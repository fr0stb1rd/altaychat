# AltayChat - Secure P2P Audio

[![Build & Deploy](https://github.com/fr0stb1rd/altaychat/actions/workflows/build.yml/badge.svg)](https://github.com/fr0stb1rd/altaychat/actions/workflows/build.yml)

![logo](docs/altaychat-logo.svg)

**AltayChat** is a cross-platform, peer-to-peer (P2P) voice chat application. It allows two people to talk securely over the internet without a central audio server. It uses modern technologies like **WebRTC** for networking and **Opus** for high-quality audio.

## ✨ Key Features

* **P2P Architecture:** Audio data goes directly from one user to another. No server listens to or stores your voice.
* **High-Quality Audio:** Powered by the **Opus codec** (48kHz Mono) for crystal clear sound even on slow connections.
* **Security:** Uses **DTLS-SRTP** for end-to-end encryption.
* **Privacy:** Supports **Cloudflare TURN** relay to hide your IP address and bypass strict firewalls.
* **Low Latency:** Uses lock-free buffers and event-driven threads for real-time performance.
* **Automated Signaling:** No need to copy-paste long SDP strings; a lightweight signaling server handles the handshake.

---

## 🛠️ Technical Stack

| Component | Technology |
| --- | --- |
| **Networking** | [libdatachannel](https://github.com/paullouisageneau/libdatachannel) (WebRTC) |
| **Audio Codec** | [libopus](https://opus-codec.org/) |
| **Audio I/O** | [PortAudio](http://www.portaudio.com/) |
| **Signaling** | Cloudflare Workers & Durable Objects |
| **Build System** | CMake |
| **Dependencies** | vcpkg (C++ Package Manager) |

---

## 🚀 Getting Started

### Prerequisites

To build the project locally, you need a C++17 compiler and **CMake**.

### Building on Linux (Arch Linux)

```bash
# Install system dependencies
sudo pacman -S libdatachannel opus portaudio nlohmann-json cmake ninja

# Build the project
cmake -B build
cmake --build build -j$(nproc)

# Run the app
./build/altaychat

```

> **Note (Local Build):** You do not need `vcpkg` to build the app on Linux or macOS. As long as you install the dependencies using your system package manager (e.g., `pacman`, `apt`, `brew`), the `CMakeLists.txt` will automatically use `pkg-config` as a fallback to locate them.

### Building on macOS

```bash
brew install libdatachannel opus portaudio nlohmann-json
cmake -B build && cmake --build build
./build/altaychat
```

### Building on Windows (with vcpkg)

```powershell
# Install dependencies via vcpkg
vcpkg install libdatachannel:x64-windows opus:x64-windows portaudio:x64-windows nlohmann-json:x64-windows

# Configure and Build
cmake -B build -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release

# Run the app
./build/Release/altaychat.exe

```

---

## 📖 How to Use

1. **Launch the App:** Open the terminal and run `altaychat`.
2. **Join a Room:** Enter a unique room name (e.g., `my-private-room-123`).
3. **Connect:** Tell your friend to join the **exact same room name**.
4. **Talk:** Once connected, the audio stream starts automatically.
5. **Exit:** Press `Ctrl+C` to close the connection and exit.

### Advanced Flags

* `--debug`: Show detailed connection and audio logs.
* `--version`: Show current version and build info.
* `--smoke-test`: Verify if all libraries and audio drivers are loaded correctly.

---

## 🔒 Security & TURN Configuration

By default, AltayChat tries to connect directly. For better connectivity and privacy, you can use **TURN** servers.

1. Create a file named `turnconfig` in the same folder as the app.
2. Add your credentials:
```ini
username=your_username
password=your_password

```



*Alternatively, you can set `TURN_USERNAME` and `TURN_PASSWORD` environment variables.*

---

## 🏗️ Project Structure

* `src/audio/`: Handles raw audio capture and playback using PortAudio.
* `src/webrtc/`: Manages P2P connection, ICE gathering, and RTP packets.
* `src/signaling/`: Communicates with the Cloudflare Worker to find peers.
* `server/`: Source code for the Cloudflare Worker signaling server.

---

## ☁️ Signaling Server

The signaling server runs on Cloudflare Workers with Durable Objects. Source is in the `server/` folder. Deploy with:

```bash
cd server
npx wrangler deploy
```

---

## 📄 License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

**Developed with ❤️ by [fr0stb1rd](https://fr0stb1rd.gitlab.io/)**
