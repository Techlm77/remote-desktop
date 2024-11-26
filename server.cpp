#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/XTest.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <atomic>
#include <thread>
#include <gtk/gtk.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <nvml.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

#define WIDTH 1280
#define HEIGHT 720
#define VIDEO_PORT 12345
#define INPUT_PORT 12346

volatile std::atomic<bool> running_flag(true);

int video_sockfd = -1;
int input_sockfd = -1;

void signal_handler(int signum);
struct InputEvent;
KeySym getKeySymFromName(const char* name);
void inputThread(Display* display, int input_sockfd);
void guiThread();
void on_destroy(GtkWidget* widget, gpointer data);

struct InputEvent {
    uint8_t type;
    union {
        struct {
            int16_t x;
            int16_t y;
        } mouse_motion;
        struct {
            uint8_t button;
            uint8_t pressed;
        } mouse_button;
        struct {
            char keyname[32];
            uint8_t pressed;
        } keyboard;
    } data;
} __attribute__((packed));

KeySym getKeySymFromName(const char* name) {
    if (strcmp(name, "Backspace") == 0) return XK_BackSpace;
    if (strcmp(name, "Tab") == 0) return XK_Tab;
    if (strcmp(name, "Return") == 0) return XK_Return;
    if (strcmp(name, "Escape") == 0) return XK_Escape;
    if (strcmp(name, "Space") == 0) return XK_space;
    if (strcmp(name, "Left Shift") == 0) return XK_Shift_L;
    if (strcmp(name, "Right Shift") == 0) return XK_Shift_R;
    if (strcmp(name, "Left Ctrl") == 0) return XK_Control_L;
    if (strcmp(name, "Right Ctrl") == 0) return XK_Control_R;
    if (strcmp(name, "Left Alt") == 0) return XK_Alt_L;
    if (strcmp(name, "Right Alt") == 0) return XK_Alt_R;
    if (strcmp(name, "Left GUI") == 0) return XK_Super_L;
    if (strcmp(name, "Right GUI") == 0) return XK_Super_R;
    if (strcmp(name, "Delete") == 0) return XK_Delete;
    if (strcmp(name, "Home") == 0) return XK_Home;
    if (strcmp(name, "End") == 0) return XK_End;
    if (strcmp(name, "Page Up") == 0) return XK_Prior;
    if (strcmp(name, "Page Down") == 0) return XK_Next;
    if (strcmp(name, "Insert") == 0) return XK_Insert;
    if (strcmp(name, "Left") == 0) return XK_Left;
    if (strcmp(name, "Right") == 0) return XK_Right;
    if (strcmp(name, "Up") == 0) return XK_Up;
    if (strcmp(name, "Down") == 0) return XK_Down;
    if (strcmp(name, "Caps Lock") == 0) return XK_Caps_Lock;
    if (strcmp(name, "Num Lock") == 0) return XK_Num_Lock;
    if (strcmp(name, "Scroll Lock") == 0) return XK_Scroll_Lock;
    if (strcmp(name, "Pause") == 0) return XK_Pause;
    if (strcmp(name, "Print Screen") == 0) return XK_Print;
    if (strcmp(name, "F1") == 0) return XK_F1;
    if (strcmp(name, "F2") == 0) return XK_F2;
    if (strcmp(name, "F3") == 0) return XK_F3;
    if (strcmp(name, "F4") == 0) return XK_F4;
    if (strcmp(name, "F5") == 0) return XK_F5;
    if (strcmp(name, "F6") == 0) return XK_F6;
    if (strcmp(name, "F7") == 0) return XK_F7;
    if (strcmp(name, "F8") == 0) return XK_F8;
    if (strcmp(name, "F9") == 0) return XK_F9;
    if (strcmp(name, "F10") == 0) return XK_F10;
    if (strcmp(name, "F11") == 0) return XK_F11;
    if (strcmp(name, "F12") == 0) return XK_F12;
    if (strcmp(name, "-") == 0) return XK_minus;
    if (strcmp(name, "=") == 0) return XK_equal;
    if (strcmp(name, "[") == 0) return XK_bracketleft;
    if (strcmp(name, "]") == 0) return XK_bracketright;
    if (strcmp(name, "\\") == 0) return XK_backslash;
    if (strcmp(name, ";") == 0) return XK_semicolon;
    if (strcmp(name, "'") == 0) return XK_apostrophe;
    if (strcmp(name, "`") == 0) return XK_grave;
    if (strcmp(name, ",") == 0) return XK_comma;
    if (strcmp(name, ".") == 0) return XK_period;
    if (strcmp(name, "/") == 0) return XK_slash;
    if (strcmp(name, "#") == 0) return XK_numbersign;
    if (strcmp(name, "Â£") == 0) return XK_sterling;
    if (strcmp(name, "KP_0") == 0) return XK_KP_0;
    if (strcmp(name, "KP_1") == 0) return XK_KP_1;
    if (strcmp(name, "KP_2") == 0) return XK_KP_2;
    if (strcmp(name, "KP_3") == 0) return XK_KP_3;
    if (strcmp(name, "KP_4") == 0) return XK_KP_4;
    if (strcmp(name, "KP_5") == 0) return XK_KP_5;
    if (strcmp(name, "KP_6") == 0) return XK_KP_6;
    if (strcmp(name, "KP_7") == 0) return XK_KP_7;
    if (strcmp(name, "KP_8") == 0) return XK_KP_8;
    if (strcmp(name, "KP_9") == 0) return XK_KP_9;
    if (strcmp(name, "KP_Decimal") == 0) return XK_KP_Decimal;
    if (strcmp(name, "KP_Divide") == 0) return XK_KP_Divide;
    if (strcmp(name, "KP_Multiply") == 0) return XK_KP_Multiply;
    if (strcmp(name, "KP_Subtract") == 0) return XK_KP_Subtract;
    if (strcmp(name, "KP_Add") == 0) return XK_KP_Add;
    if (strcmp(name, "KP_Enter") == 0) return XK_KP_Enter;
    if (strcmp(name, "KP_Equal") == 0) return XK_KP_Equal;

    if (strlen(name) == 1) {
        return XStringToKeysym(name);
    }

    return NoSymbol;
}

void inputThread(Display* display, int input_sockfd) {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    while (running_flag) {
        InputEvent inputEvent;
        ssize_t received = recvfrom(input_sockfd, &inputEvent, sizeof(InputEvent), 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (received <= 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recvfrom input");
                running_flag = false;
                break;
            }
            usleep(1000);
            continue;
        }

        switch (inputEvent.type) {
            case 0:
                XTestFakeMotionEvent(display, -1, inputEvent.data.mouse_motion.x, inputEvent.data.mouse_motion.y, CurrentTime);
                XFlush(display);
                break;
            case 1:
                XTestFakeButtonEvent(display, inputEvent.data.mouse_button.button, inputEvent.data.mouse_button.pressed, CurrentTime);
                XFlush(display);
                break;
            case 2: {
                KeySym keysym = getKeySymFromName(inputEvent.data.keyboard.keyname);
                if (keysym == NoSymbol) {
                    fprintf(stderr, "Unknown key name: %s\n", inputEvent.data.keyboard.keyname);
                    break;
                }
                KeyCode keycode = XKeysymToKeycode(display, keysym);
                if (keycode == 0) {
                    fprintf(stderr, "No keycode for keysym: %lu\n", keysym);
                    break;
                }
                XTestFakeKeyEvent(display, keycode, inputEvent.data.keyboard.pressed, CurrentTime);
                XFlush(display);
                break;
            }
            default:
                break;
        }
    }
}

double getCPUUsage() {
    static long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;
    double percent;
    FILE* file = fopen("/proc/stat", "r");
    if (!file) return 0.0;
    char buffer[1024];
    fgets(buffer, sizeof(buffer), file);
    fclose(file);
    long long user, nice, sys, idle;
    sscanf(buffer, "cpu  %lld %lld %lld %lld", &user, &nice, &sys, &idle);
    long long totalUser = user - lastTotalUser;
    long long totalUserLow = nice - lastTotalUserLow;
    long long totalSys = sys - lastTotalSys;
    long long totalIdle = idle - lastTotalIdle;
    long long total = totalUser + totalUserLow + totalSys + totalIdle;
    percent = (double)(totalUser + totalSys) / total * 100.0;
    lastTotalUser = user;
    lastTotalUserLow = nice;
    lastTotalSys = sys;
    lastTotalIdle = idle;
    return percent;
}

double getRAMUsage() {
    FILE* file = fopen("/proc/meminfo", "r");
    if (!file) return 0.0;
    char buffer[256];
    long long memTotal = 0, memAvailable = 0;
    while (fgets(buffer, sizeof(buffer), file)) {
        if (sscanf(buffer, "MemTotal: %lld kB", &memTotal) == 1) {}
        if (sscanf(buffer, "MemAvailable: %lld kB", &memAvailable) == 1) break;
    }
    fclose(file);
    if (memTotal == 0) return 0.0;
    double used = (double)(memTotal - memAvailable) / memTotal * 100.0;
    return used;
}

double getGPUUsage() {
    nvmlReturn_t result;
    unsigned int device_count, i;
    double gpu_usage = 0.0;

    result = nvmlInit();
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(result));
        return 0.0;
    }

    result = nvmlDeviceGetCount(&device_count);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get device count: %s\n", nvmlErrorString(result));
        nvmlShutdown();
        return 0.0;
    }

    for (i = 0; i < device_count; ++i) {
        nvmlDevice_t device;
        result = nvmlDeviceGetHandleByIndex(i, &device);
        if (result != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get device handle: %s\n", nvmlErrorString(result));
            continue;
        }

        nvmlUtilization_t utilization;
        result = nvmlDeviceGetUtilizationRates(device, &utilization);
        if (result != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get utilization rates: %s\n", nvmlErrorString(result));
            continue;
        }

        gpu_usage += utilization.gpu;
    }

    nvmlShutdown();

    if (device_count > 0) {
        gpu_usage /= device_count;
    }

    return gpu_usage;
}

GtkWidget* cpu_label;
GtkWidget* ram_label;
GtkWidget* gpu_label;

gboolean update_gui_labels(gpointer data) {
    double cpu = getCPUUsage();
    double ram = getRAMUsage();
    double gpu = getGPUUsage();

    std::stringstream ss_cpu;
    ss_cpu << "CPU Usage: " << cpu << " %";
    gtk_label_set_text(GTK_LABEL(cpu_label), ss_cpu.str().c_str());

    std::stringstream ss_ram;
    ss_ram << "RAM Usage: " << ram << " %";
    gtk_label_set_text(GTK_LABEL(ram_label), ss_ram.str().c_str());

    std::stringstream ss_gpu;
    ss_gpu << "GPU Usage: " << gpu << " %";
    gtk_label_set_text(GTK_LABEL(gpu_label), ss_gpu.str().c_str());

    return TRUE;
}

void on_destroy(GtkWidget* widget, gpointer data) {
    running_flag = false;
    if (video_sockfd != -1) {
        close(video_sockfd);
        video_sockfd = -1;
    }
    if (input_sockfd != -1) {
        close(input_sockfd);
        input_sockfd = -1;
    }
    gtk_main_quit();
}

void guiThread() {
    gtk_init(NULL, NULL);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Server Resource Usage");
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    gtk_window_set_default_size(GTK_WINDOW(window), 200, 100);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    cpu_label = gtk_label_new("CPU Usage: 0 %");
    gtk_box_pack_start(GTK_BOX(vbox), cpu_label, TRUE, TRUE, 0);

    ram_label = gtk_label_new("RAM Usage: 0 %");
    gtk_box_pack_start(GTK_BOX(vbox), ram_label, TRUE, TRUE, 0);

    gpu_label = gtk_label_new("GPU Usage: 0 %");
    gtk_box_pack_start(GTK_BOX(vbox), gpu_label, TRUE, TRUE, 0);

    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    g_timeout_add_seconds(1, update_gui_labels, NULL);

    gtk_widget_show_all(window);
    gtk_main();
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::thread gui(guiThread);

    video_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (video_sockfd < 0) {
        perror("Failed to create video socket");
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    input_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (input_sockfd < 0) {
        perror("Failed to create input socket");
        close(video_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    int flags = fcntl(input_sockfd, F_GETFL, 0);
    if (flags == -1 || fcntl(input_sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl input_sockfd");
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    struct sockaddr_in serverAddr;
    socklen_t clientAddrLen = sizeof(struct sockaddr_in);

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    serverAddr.sin_port = htons(VIDEO_PORT);
    if (bind(video_sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind video_sockfd failed");
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    if (listen(video_sockfd, 1) < 0) {
        perror("Listen failed");
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    serverAddr.sin_port = htons(INPUT_PORT);
    if (bind(input_sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind input_sockfd failed");
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    std::cout << "Server listening on port " << VIDEO_PORT << " (video) and bound to port " << INPUT_PORT << " (input).\n";

    Display* display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open X display\n");
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    int event_base, error_base;
    if (!XTestQueryExtension(display, &event_base, &error_base, &event_base, &event_base)) {
        fprintf(stderr, "XTest extension not available\n");
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    Window root = DefaultRootWindow(display);

    XShmSegmentInfo shminfo;
    memset(&shminfo, 0, sizeof(shminfo));

    if (!XShmQueryExtension(display)) {
        fprintf(stderr, "XShm extension not supported\n");
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    XImage* img = XShmCreateImage(display, DefaultVisual(display, DefaultScreen(display)),
                                  DefaultDepth(display, DefaultScreen(display)), ZPixmap, NULL, &shminfo, WIDTH, HEIGHT);
    if (!img) {
        fprintf(stderr, "XShmCreateImage failed\n");
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
    if (shminfo.shmid < 0) {
        perror("shmget failed");
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    shminfo.shmaddr = (char*)shmat(shminfo.shmid, NULL, 0);
    if (shminfo.shmaddr == (char*)-1) {
        perror("shmat failed");
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    img->data = shminfo.shmaddr;
    shminfo.readOnly = False;

    if (!XShmAttach(display, &shminfo)) {
        fprintf(stderr, "Failed to attach XShm\n");
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    XSync(display, False);

    avformat_network_init();

    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!codec) {
        fprintf(stderr, "Failed to find h264_nvenc encoder\n");
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        fprintf(stderr, "Failed to allocate codec context\n");
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    codecCtx->bit_rate = 5000000;
    codecCtx->width = WIDTH;
    codecCtx->height = HEIGHT;
    codecCtx->time_base = {1, 30};
    codecCtx->framerate = {30, 1};
    codecCtx->gop_size = 30;
    codecCtx->max_b_frames = 0;
    codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    av_opt_set(codecCtx->priv_data, "preset", "p1", 0);
    av_opt_set(codecCtx->priv_data, "tune", "ull", 0);
    av_opt_set(codecCtx->priv_data, "rc", "cbr", 0);

    if (avcodec_open2(codecCtx, codec, NULL) < 0) {
        fprintf(stderr, "Failed to open codec\n");
        avcodec_free_context(&codecCtx);
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    /* Allocate frames */
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Failed to allocate frame\n");
        avcodec_free_context(&codecCtx);
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    frame->format = codecCtx->pix_fmt;
    frame->width = codecCtx->width;
    frame->height = codecCtx->height;

    if (av_frame_get_buffer(frame, 32) < 0) {
        fprintf(stderr, "Failed to allocate frame buffer\n");
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    struct SwsContext* swsCtx = sws_getContext(
        WIDTH, HEIGHT, AV_PIX_FMT_BGRA,
        WIDTH, HEIGHT, AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR, NULL, NULL, NULL
    );
    if (!swsCtx) {
        fprintf(stderr, "Failed to initialize sws context\n");
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Failed to allocate AVPacket\n");
        sws_freeContext(swsCtx);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, NULL);
        XDestroyImage(img);
        XCloseDisplay(display);
        close(video_sockfd);
        close(input_sockfd);
        running_flag = false;
        gui.join();
        return EXIT_FAILURE;
    }

    std::thread input_thread_handle(inputThread, display, input_sockfd);

    while (running_flag) {
        std::cout << "Waiting for client to connect...\n";

        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        int client_sockfd = accept(video_sockfd, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (client_sockfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Accept failed");
            running_flag = false;
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), client_ip, INET_ADDRSTRLEN);
        std::cout << "Client connected: " << client_ip << ":" << ntohs(clientAddr.sin_port) << "\n";

        std::cout << "Starting screen capture and transmission...\n";

        int frameIndex = 0;
        bool client_connected = true;

        while (running_flag && client_connected) {
            if (!XShmGetImage(display, root, img, 0, 0, AllPlanes)) {
                fprintf(stderr, "XShmGetImage failed\n");
                continue;
            }

            uint8_t* srcData[1] = { reinterpret_cast<uint8_t*>(img->data) };
            int srcLinesize[1] = { img->bytes_per_line };
            sws_scale(swsCtx, srcData, srcLinesize, 0, HEIGHT, frame->data, frame->linesize);

            frame->pts = frameIndex++;

            if (avcodec_send_frame(codecCtx, frame) < 0) {
                fprintf(stderr, "Error sending frame to encoder\n");
                continue;
            }

            while (avcodec_receive_packet(codecCtx, pkt) == 0) {
                int dataSize = pkt->size;
                uint32_t pkt_size = htonl(dataSize);
                ssize_t sent = send(client_sockfd, &pkt_size, sizeof(pkt_size), 0);
                if (sent <= 0) {
                    perror("send failed");
                    client_connected = false;
                    break;
                }

                int total_sent = 0;
                while (total_sent < dataSize) {
                    sent = send(client_sockfd, pkt->data + total_sent, dataSize - total_sent, 0);
                    if (sent <= 0) {
                        perror("send failed");
                        client_connected = false;
                        break;
                    }
                    total_sent += sent;
                }

                av_packet_unref(pkt);

                if (!client_connected) {
                    break;
                }
            }

            usleep(33333);

            char buffer[1];
            int result = recv(client_sockfd, buffer, 1, MSG_PEEK | MSG_DONTWAIT);
            if (result == 0) {
                std::cout << "Client disconnected.\n";
                client_connected = false;
            } else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recv failed");
                client_connected = false;
            }
        }

        close(client_sockfd);
        std::cout << "Client connection closed.\n";
    }

    std::cout << "Shutting down server...\n";

    running_flag = false;
    if (input_thread_handle.joinable()) {
        input_thread_handle.join();
    }
    if (gui.joinable()) {
        gui.join();
    }
    av_packet_free(&pkt);
    sws_freeContext(swsCtx);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    XShmDetach(display, &shminfo);
    shmdt(shminfo.shmaddr);
    shmctl(shminfo.shmid, IPC_RMID, NULL);
    XDestroyImage(img);
    XCloseDisplay(display);
    if (video_sockfd != -1) {
        close(video_sockfd);
    }
    if (input_sockfd != -1) {
        close(input_sockfd);
    }

    std::cout << "Server shutdown complete.\n";
    return EXIT_SUCCESS;
}

void signal_handler(int signum) {
    running_flag = false;
    if (video_sockfd != -1) {
        close(video_sockfd);
        video_sockfd = -1;
    }
    if (input_sockfd != -1) {
        close(input_sockfd);
        input_sockfd = -1;
    }
}
