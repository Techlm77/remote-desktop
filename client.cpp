#include <gst/gst.h>
#include <gst/video/video.h>
#include <string>
#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

std::atomic<bool> running(true);
int control_sockfd;
struct sockaddr_in servaddr_control;

void signal_handler(int signum){
    running = false;
    close(control_sockfd);
    gst_deinit();
    exit(0);
}

std::string get_gpu_type(){
    FILE* fp = popen("lspci | grep -E 'VGA|3D|Display'", "r");
    if(!fp) return "unknown";
    char buffer[128];
    std::string result = "";
    while(fgets(buffer, sizeof(buffer), fp)!=NULL){
        result += buffer;
    }
    pclose(fp);
    if(result.find("NVIDIA") != std::string::npos) return "nvidia";
    if(result.find("AMD") != std::string::npos) return "amd";
    if(result.find("Intel") != std::string::npos) return "intel";
    return "unknown";
}

bool check_h264_decoder(){
    GstElementFactory* factory = gst_element_factory_find("avdec_h264");
    if(factory){
        gst_object_unref(factory);
        return true;
    }
    factory = gst_element_factory_find("vaapih264dec");
    if(factory){
        gst_object_unref(factory);
        return true;
    }
    return false;
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data){
    switch (GST_MESSAGE_TYPE(msg)){
        case GST_MESSAGE_ERROR:{
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            std::cerr << "GStreamer Error: " << err->message << std::endl;
            g_error_free(err);
            g_free(debug);
            running = false;
            break;
        }
        case GST_MESSAGE_EOS:
            running = false;
            break;
        default:
            break;
    }
    return TRUE;
}

int main(int argc, char *argv[]){
    if(argc != 3){
        std::cerr << "Usage: ./client <server_ip> <control_port>" << std::endl;
        return -1;
    }
    signal(SIGINT, signal_handler);
    gst_init(&argc, &argv);
    std::string gpu = get_gpu_type();
    std::cout << "Detected GPU: " << gpu << std::endl;
    if(!check_h264_decoder()){
        std::cerr << "H.264 decoder not supported on this GPU" << std::endl;
        return -1;
    }

    std::string server_ip = argv[1];
    int control_port = atoi(argv[2]);

    control_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(control_sockfd < 0){
        std::cerr << "Failed to create control socket" << std::endl;
        return -1;
    }

    memset(&servaddr_control, 0, sizeof(servaddr_control));
    servaddr_control.sin_family = AF_INET;
    servaddr_control.sin_port = htons(control_port);
    if(inet_pton(AF_INET, server_ip.c_str(), &servaddr_control.sin_addr) <= 0){
        std::cerr << "Invalid server IP address" << std::endl;
        close(control_sockfd);
        return -1;
    }

    const char *init_msg = "start";
    sendto(control_sockfd, init_msg, strlen(init_msg), 0, (const struct sockaddr *)&servaddr_control, sizeof(servaddr_control));
    std::cout << "Sent initial control message to server" << std::endl;

    GstElement *pipeline, *udpsrc, *capsfilter, *queue1, *rtpjitterbuffer, *queue2, *depay, *queue3, *decoder, *queue4, *videoconvert, *queue5, *videosink;
    GstBus *bus;
    GstMessage *msg;

    pipeline = gst_pipeline_new("client-pipeline");
    udpsrc = gst_element_factory_make("udpsrc", "udpsrc");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    queue1 = gst_element_factory_make("queue", "queue1");
    rtpjitterbuffer = gst_element_factory_make("rtpjitterbuffer", "jitter");
    queue2 = gst_element_factory_make("queue", "queue2");
    depay = gst_element_factory_make("rtph264depay", "depay");
    queue3 = gst_element_factory_make("queue", "queue3");
    decoder = gst_element_factory_make("avdec_h264", "decoder");
    queue4 = gst_element_factory_make("queue", "queue4");
    videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
    queue5 = gst_element_factory_make("queue", "queue5");
    videosink = gst_element_factory_make("xvimagesink", "videosink");

    if(!pipeline || !udpsrc || !capsfilter || !queue1 || !rtpjitterbuffer || !queue2 || !depay || !queue3 || !decoder || !queue4 || !videoconvert || !queue5 || !videosink){
        std::cerr << "Failed to create GStreamer elements" << std::endl;
        return -1;
    }

    g_object_set(udpsrc, "port", 5000, NULL);
    GstCaps *caps = gst_caps_new_simple("application/x-rtp",
                                        "payload", G_TYPE_INT, 96,
                                        NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), udpsrc, capsfilter, queue1, rtpjitterbuffer, queue2, depay, queue3, decoder, queue4, videoconvert, queue5, videosink, NULL);

    if(!gst_element_link_many(udpsrc, capsfilter, queue1, rtpjitterbuffer, queue2, depay, queue3, decoder, queue4, videoconvert, queue5, videosink, NULL)){
        std::cerr << "Failed to link GStreamer elements" << std::endl;
        gst_object_unref(pipeline);
        return -1;
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, NULL);
    gst_object_unref(bus);

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if(ret == GST_STATE_CHANGE_FAILURE){
        std::cerr << "Failed to set pipeline to PLAYING" << std::endl;
        gst_object_unref(pipeline);
        return -1;
    }

    std::cout << "Client is receiving video from server on port 5000" << std::endl;

    Display* display = XOpenDisplay(NULL);
    if(!display){
        std::cerr << "Failed to open X display" << std::endl;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return -1;
    }

    std::thread input_thread([&](){
        while(running){
            XEvent event;
            XNextEvent(display, &event);
            if(event.type == MotionNotify){
                int x = event.xmotion.x;
                int y = event.xmotion.y;
                char m_buffer[9];
                m_buffer[0] = 'm';
                memcpy(m_buffer+1, &x, sizeof(int));
                memcpy(m_buffer+5, &y, sizeof(int));
                sendto(control_sockfd, m_buffer, sizeof(m_buffer), 0, (const struct sockaddr *)&servaddr_control, sizeof(servaddr_control));
            }
            if(event.type == KeyPress){
                int key = XLookupKeysym(&event.xkey, 0);
                char k_buffer[5];
                k_buffer[0] = 'k';
                memcpy(k_buffer+1, &key, sizeof(int));
                sendto(control_sockfd, k_buffer, sizeof(k_buffer), 0, (const struct sockaddr *)&servaddr_control, sizeof(servaddr_control));
            }
        }
    });

    std::cout << "Client is connected to server at " << server_ip << ":" << control_port << std::endl;

    while(running){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    XCloseDisplay(display);
    close(control_sockfd);
    input_thread.join();
    return 0;
}
