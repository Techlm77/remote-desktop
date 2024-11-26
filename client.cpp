#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#define WIDTH 1280
#define HEIGHT 720
#define VIDEO_PORT 12345
#define INPUT_PORT 12346

#define SERVER_IP "public/local ip address goes here"

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

std::queue<AVPacket*> packetQueue;
std::mutex queueMutex;
std::condition_variable queueCondVar;
std::atomic<bool> running_flag(true);

void networkThread(int sockfd);
void inputThread(int input_sockfd, struct sockaddr_in inputAddr, SDL_Window* window);

int main() {
    int video_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (video_sockfd < 0) {
        perror("Failed to create video socket");
        return EXIT_FAILURE;
    }

    int input_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (input_sockfd < 0) {
        perror("Failed to create input socket");
        close(video_sockfd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in serverAddr, inputAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(VIDEO_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    memset(&inputAddr, 0, sizeof(inputAddr));
    inputAddr.sin_family = AF_INET;
    inputAddr.sin_port = htons(INPUT_PORT);
    inputAddr.sin_addr = serverAddr.sin_addr;

    if (connect(video_sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connect failed");
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    std::cout << "Connected to server at " << SERVER_IP << ":" << VIDEO_PORT << "\n";

    avformat_network_init();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Remote Desktop",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        WIDTH,
        HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        WIDTH,
        HEIGHT
    );

    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    std::thread netThread(networkThread, video_sockfd);

    std::thread inpThread(inputThread, input_sockfd, inputAddr, window);

    std::cout << "Starting to receive and display frames...\n";

    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!decoder) {
        fprintf(stderr, "Failed to find H.264 decoder\n");
        running_flag = false;
        netThread.join();
        inpThread.join();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(decoder);
    if (!codecCtx) {
        fprintf(stderr, "Failed to allocate codec context\n");
        running_flag = false;
        netThread.join();
        inpThread.join();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    if (avcodec_open2(codecCtx, decoder, NULL) < 0) {
        fprintf(stderr, "Failed to open codec\n");
        avcodec_free_context(&codecCtx);
        running_flag = false;
        netThread.join();
        inpThread.join();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        close(video_sockfd);
        close(input_sockfd);
        return EXIT_FAILURE;
    }

    while (running_flag) {
        AVPacket* packet = nullptr;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (queueCondVar.wait_for(lock, std::chrono::milliseconds(100), [] { return !packetQueue.empty(); })) {
                packet = packetQueue.front();
                packetQueue.pop();
            }
        }

        if (packet) {
            if (avcodec_send_packet(codecCtx, packet) < 0) {
                fprintf(stderr, "Error sending packet to decoder\n");
                av_packet_free(&packet);
                continue;
            }

            av_packet_free(&packet);

            while (true) {
                AVFrame* frame = av_frame_alloc();
                if (!frame) {
                    fprintf(stderr, "Failed to allocate frame\n");
                    break;
                }

                int ret = avcodec_receive_frame(codecCtx, frame);
                if (ret == 0) {
                    if (frame->format == AV_PIX_FMT_YUV420P) {
                        SDL_UpdateYUVTexture(
                            texture,
                            NULL,
                            frame->data[0],
                            frame->linesize[0],
                            frame->data[1],
                            frame->linesize[1],
                            frame->data[2],
                            frame->linesize[2]
                        );

                        SDL_RenderClear(renderer);
                        SDL_RenderCopy(renderer, texture, NULL, NULL);
                        SDL_RenderPresent(renderer);
                    } else {
                        fprintf(stderr, "Unexpected frame format: %s\n", av_get_pix_fmt_name((AVPixelFormat)frame->format));
                    }

                    av_frame_free(&frame);
                } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&frame);
                    break;
                } else {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    fprintf(stderr, "Error decoding frame: %s\n", errbuf);
                    av_frame_free(&frame);
                    break;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::cout << "Shutting down client...\n";

    running_flag = false;
    if (netThread.joinable()) {
        netThread.join();
    }
    if (inpThread.joinable()) {
        inpThread.join();
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    avcodec_free_context(&codecCtx);
    close(video_sockfd);
    close(input_sockfd);

    std::cout << "Client shutdown complete.\n";
    return EXIT_SUCCESS;
}

void networkThread(int sockfd) {
    while (running_flag) {
        uint32_t pkt_size_net;
        ssize_t received = recv(sockfd, &pkt_size_net, sizeof(pkt_size_net), MSG_WAITALL);
        if (received <= 0) {
            if (received == 0) {
                std::cout << "Server closed the connection\n";
            } else {
                perror("recv failed");
            }
            running_flag = false;
            break;
        }

        uint32_t pkt_size = ntohl(pkt_size_net);
        if (pkt_size <= 0) {
            std::cerr << "Invalid packet size received: " << pkt_size << "\n";
            continue;
        }

        uint8_t* pkt_data = new uint8_t[pkt_size];
        size_t total_received = 0;
        while (total_received < pkt_size) {
            ssize_t bytes = recv(sockfd, pkt_data + total_received, pkt_size - total_received, 0);
            if (bytes <= 0) {
                if (bytes == 0) {
                    std::cout << "Server closed the connection\n";
                } else {
                    perror("recv failed");
                }
                running_flag = false;
                delete[] pkt_data;
                break;
            }
            total_received += bytes;
        }

        if (!running_flag) {
            delete[] pkt_data;
            break;
        }

        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            std::cerr << "Failed to allocate AVPacket\n";
            delete[] pkt_data;
            continue;
        }
        packet->data = pkt_data;
        packet->size = pkt_size;

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (packetQueue.size() < 100) {
                packetQueue.push(packet);
            } else {
                av_packet_free(&packet);
                std::cerr << "Packet queue full. Dropping packet to reduce latency.\n";
            }
        }
        queueCondVar.notify_one();
    }
}

void inputThread(int input_sockfd, struct sockaddr_in inputAddr, SDL_Window* window) {
    while (running_flag) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            InputEvent inputEvent;
            memset(&inputEvent, 0, sizeof(InputEvent));

            switch (event.type) {
                case SDL_QUIT:
                    running_flag = false;
                    break;
                case SDL_MOUSEMOTION: {
                    inputEvent.type = 0;
                    inputEvent.data.mouse_motion.x = static_cast<int16_t>(event.motion.x);
                    inputEvent.data.mouse_motion.y = static_cast<int16_t>(event.motion.y);
                    sendto(input_sockfd, &inputEvent, sizeof(InputEvent), 0,
                           (struct sockaddr*)&inputAddr, sizeof(inputAddr));
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    inputEvent.type = 1;
                    inputEvent.data.mouse_button.button = event.button.button;
                    inputEvent.data.mouse_button.pressed =
                        (event.type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
                    sendto(input_sockfd, &inputEvent, sizeof(InputEvent), 0,
                           (struct sockaddr*)&inputAddr, sizeof(inputAddr));
                    break;
                case SDL_MOUSEWHEEL:
                    inputEvent.type = 1;
                    if (event.wheel.y > 0) {
                        inputEvent.data.mouse_button.button = 4;
                    } else if (event.wheel.y < 0) {
                        inputEvent.data.mouse_button.button = 5;
                    } else {
                        break;
                    }
                    inputEvent.data.mouse_button.pressed = 1;
                    sendto(input_sockfd, &inputEvent, sizeof(InputEvent), 0,
                           (struct sockaddr*)&inputAddr, sizeof(inputAddr));
                    inputEvent.data.mouse_button.pressed = 0;
                    sendto(input_sockfd, &inputEvent, sizeof(InputEvent), 0,
                           (struct sockaddr*)&inputAddr, sizeof(inputAddr));
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    inputEvent.type = 2;
                    const char* keyName = SDL_GetKeyName(event.key.keysym.sym);
                    if (keyName &&
                        strlen(keyName) < sizeof(inputEvent.data.keyboard.keyname)) {
                        strcpy(inputEvent.data.keyboard.keyname, keyName);
                    } else {
                        snprintf(inputEvent.data.keyboard.keyname,
                                 sizeof(inputEvent.data.keyboard.keyname), "Unknown");
                    }
                    inputEvent.data.keyboard.pressed =
                        (event.type == SDL_KEYDOWN) ? 1 : 0;
                    sendto(input_sockfd, &inputEvent, sizeof(InputEvent), 0,
                           (struct sockaddr*)&inputAddr, sizeof(inputAddr));
                    break;
                }
                default:
                    break;
            }
        }
    }
}