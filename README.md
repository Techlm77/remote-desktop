
# Remote Desktop Streaming Project

This is a **remote desktop streaming project** designed to allow seamless desktop sharing and interaction over a network. The project is primarily developed for **Ubuntu 24.04 Desktop**, leveraging **GStreamer** for video streaming and **X11** for input capture. The system is designed to work "out of the box" with minimal configuration once the required packages are installed.

---

## Features

- **Real-Time Desktop Streaming**: Stream your desktop to another device with minimal latency.
- **Input Redirection**: Supports remote mouse and keyboard control.
- **Cross-GPU Compatibility**: Allows streaming across devices with different GPUs (with certain limitations – see below).
- **Lightweight**: Optimised for minimal system resource usage.

---

## Requirements

This project is currently under active development. To ensure compatibility, please follow these requirements:

### System Requirements
- **Operating System**: Ubuntu 24.04 Desktop
- **GStreamer Support**: Ensure hardware acceleration and required plugins are installed.

### Supported GPU Configurations
| Server GPU      | Client GPU         | Compatibility |
|------------------|--------------------|---------------|
| **NVIDIA**       | NVIDIA             | ✅            |
| **NVIDIA**       | Intel              | ✅            |
| **NVIDIA**       | AMD                | ✅            |
| **Intel**        | Intel              | ❌            |
| **Intel**        | NVIDIA             | ❌            |
| **Intel**        | AMD                | ❌            |
| **AMD**          | AMD                | ❌            |
| **AMD**          | NVIDIA             | ❌            |
| **AMD**          | Intel              | ❌            |

**Note:** Currently, only **NVIDIA GPUs** on the server side are fully supported. Compatibility for **Intel** and **AMD GPUs** is under development.

---

## Installation Instructions

1. **Install Dependencies**
   First, ensure all required GStreamer plugins and dependencies are installed:
   ```bash
   sudo apt update
   sudo apt install build-essential libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libx11-dev libxtst-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-tools
   ```

2. **Clone the Repository**
   ```bash
   git clone https://github.com/Techlm77/remote-desktop.git
   cd remote-desktop
   ```

3. **Compile the Project**
   Use the following commands to compile the server and client programs:
   ```bash
   g++ server.cpp -o server `pkg-config --cflags --libs gstreamer-1.0` -lX11 -lXtst -pthread
   g++ client.cpp -o client `pkg-config --cflags --libs gstreamer-1.0` -lX11 -lXtst -pthread
   ```

4. **Run the Applications**
   - Start the server:
     ```bash
     ./server
     ```
   - Start the client:
     ```bash
     ./client <server_ip> <control_port>
     ```

---

## Usage

1. **Server Side**:
   - Run the server on the machine you want to stream the desktop from.
   - The server will listen for control messages on port `6000` by default and stream video on port `5000`.

2. **Client Side**:
   - Run the client on the machine you want to view the desktop on.
   - Pass the server's IP and control port as arguments:
     ```bash
     ./client <server_ip> 6000
     ```

3. **Interaction**:
   - The client can send mouse and keyboard inputs to the server for remote control.

---

## Known Issues and Limitations

- **GPU Support**: Currently, only NVIDIA GPUs on the server side are supported. Development for Intel and AMD GPUs is ongoing.
- **Ubuntu Only**: This project is tailored for Ubuntu 24.04 and may require additional adjustments on other distributions.
- **Latency**: While optimised for minimal latency, network conditions can impact performance.

---

## Future Developments

- Expanding GPU support for **Intel** and **AMD GPUs**.
- Adding support for multiple operating systems.
- Improving user interface and configuration options.

---

## Contributing

Contributions are welcome! Feel free to fork this repository, make changes, and submit a pull request. For major changes, please open an issue to discuss them first.

---

## Acknowledgements

Special thanks to the open-source community for the tools and libraries that make this project possible. Honestly, if it wasn’t for the open-source community, I’d probably be pulling my hair out XD.

---

Feel free to raise any issues or suggestions on the [GitHub issues page](https://github.com/Techlm77/remote-desktop/issues). Thank you for your support!
