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
std::string client_ip;

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

bool check_h264_encoder(){
    GstElementFactory* factory = gst_element_factory_find("x264enc");
    if(factory){
        gst_object_unref(factory);
        return true;
    }
    factory = gst_element_factory_find("vaapih264enc");
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
    signal(SIGINT, signal_handler);
    gst_init(&argc, &argv);
    std::string gpu = get_gpu_type();
    std::cout << "Detected GPU: " << gpu << std::endl;
    if(!check_h264_encoder()){
        std::cerr << "H.264 encoder not supported on this GPU" << std::endl;
        return -1;
    }

    control_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(control_sockfd < 0){
        std::cerr << "Failed to create control socket" << std::endl;
        return -1;
    }

    struct sockaddr_in servaddr_control, cliaddr_control;
    memset(&servaddr_control, 0, sizeof(servaddr_control));
    servaddr_control.sin_family = AF_INET;
    servaddr_control.sin_addr.s_addr = INADDR_ANY;
    servaddr_control.sin_port = htons(6000);

    if(bind(control_sockfd, (const struct sockaddr *)&servaddr_control, sizeof(servaddr_control)) < 0){
        std::cerr << "Failed to bind control socket" << std::endl;
        close(control_sockfd);
        return -1;
    }

    std::cout << "Server is listening for control messages on port 6000" << std::endl;

    socklen_t len = sizeof(cliaddr_control);
    char buffer[1024];
    int n = recvfrom(control_sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&cliaddr_control, &len);
    if(n < 0){
        std::cerr << "Failed to receive initial control message" << std::endl;
        close(control_sockfd);
        return -1;
    }

    client_ip = inet_ntoa(cliaddr_control.sin_addr);
    std::cout << "Client connected from " << client_ip << std::endl;

    GstElement *pipeline, *source, *queue1, *videoconvert, *queue2, *encoder, *queue3, *rtp, *queue4, *udpsink;
    GstBus *bus;
    GstMessage *msg;

    pipeline = gst_pipeline_new("server-pipeline");
    source = gst_element_factory_make("ximagesrc", "source");
    queue1 = gst_element_factory_make("queue", "queue1");
    videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
    queue2 = gst_element_factory_make("queue", "queue2");
    encoder = gst_element_factory_make("x264enc", "encoder");
    queue3 = gst_element_factory_make("queue", "queue3");
    rtp = gst_element_factory_make("rtph264pay", "rtp");
    queue4 = gst_element_factory_make("queue", "queue4");
    udpsink = gst_element_factory_make("udpsink", "udpsink");

    if(!pipeline || !source || !queue1 || !videoconvert || !queue2 || !encoder || !queue3 || !rtp || !queue4 || !udpsink){
        std::cerr << "Failed to create GStreamer elements" << std::endl;
        return -1;
    }

    g_object_set(source, "use-damage", FALSE, NULL);
    g_object_set(encoder, "tune", 0x00000004, NULL);
    g_object_set(encoder, "bitrate", 500, NULL);
    g_object_set(rtp, "pt", 96, NULL);
    g_object_set(udpsink, "port", 5000, "host", client_ip.c_str(), "async", FALSE, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(pipeline), source, queue1, videoconvert, queue2, encoder, queue3, rtp, queue4, udpsink, NULL);

    if(!gst_element_link_many(source, queue1, videoconvert, queue2, encoder, queue3, rtp, queue4, udpsink, NULL)){
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

    std::cout << "Server is streaming desktop to " << client_ip << ":5000" << std::endl;

    Display* display = XOpenDisplay(NULL);
    if(!display){
        std::cerr << "Failed to open X display" << std::endl;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return -1;
    }

    while(running){
        char ctrl_buffer[9];
        int bytes = recvfrom(control_sockfd, ctrl_buffer, sizeof(ctrl_buffer), 0, (struct sockaddr *)&cliaddr_control, &len);
        if(bytes > 0){
            if(ctrl_buffer[0] == 'm' && bytes >= 9){
                int x, y;
                memcpy(&x, ctrl_buffer+1, sizeof(int));
                memcpy(&y, ctrl_buffer+5, sizeof(int));
                XTestFakeMotionEvent(display, -1, x, y, CurrentTime);
                XFlush(display);
            }
            if(ctrl_buffer[0] == 'k' && bytes >= 5){
                int key;
                memcpy(&key, ctrl_buffer+1, sizeof(int));
                XTestFakeKeyEvent(display, key, True, CurrentTime);
                XTestFakeKeyEvent(display, key, False, CurrentTime);
                XFlush(display);
            }
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    XCloseDisplay(display);
    close(control_sockfd);
    return 0;
}
