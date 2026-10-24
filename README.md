# DroidCam Clone

An independent Android-to-Windows camera stream prototype. This project is not wire-compatible with DroidCam's official apps and contains no proprietary DroidCam code.

## Current target

- Android sender and Windows preview receiver.
- H.264 at 1920×1080, targeting 60 fps and offering 30 fps when the camera and encoder support it.
- Versioned TCP session/control and RTP/H.264 over UDP on a trusted local network.
- Windows preview currently decodes to RGB32 and presents the newest available frame through GDI; D3D11 presentation remains a follow-up optimization.
- No audio, discovery, USB/ADB, background streaming, or virtual webcam output in this milestone.

See [clone protocol](docs/research/droidcam/clone-protocol.md), [design proposal](docs/superpowers/specs/2026-10-24-droidcam-clone-vertical-slice.md), and [implementation plan](docs/superpowers/plans/2026-10-24-droidcam-clone-vertical-slice.md).
Implementation evidence and limits are tracked in [clone implementation status](docs/research/droidcam/clone-implementation-status.md).

## CI/CD and releases

Pull requests and pushes to `main` build both clients. Pushing a signed version tag such as `v0.1.0` publishes the Android APK and Windows x64 ZIP as a GitHub Release. Configure the one-time Android signing secrets by following the [CI/CD guide](docs/ci-cd.md).

## Build prerequisites

- Android: JDK 17+, Android SDK Platform 36, Android Build Tools 36, Gradle wrapper.
- Windows: Visual Studio Build Tools with the C++ x64 workload and Windows SDK; CMake 3.24+.

On Windows, allow the receiver executable through the private-network firewall. It listens on TCP port 48200 and negotiates an ephemeral UDP video port for each session.

## Build

Android debug APK:

```powershell
cd android
.\gradlew.bat assembleDebug
```

Windows x64 debug preview:

```powershell
cmake -S windows -B windows/build -G "Visual Studio 17 2022" -A x64
cmake --build windows/build --config Debug
```

## Use

1. Start `windows/build/Debug/droidcam_clone.exe`.
2. On Android, enter the Windows PC's IPv4 address, choose an available 1080p profile, and tap **Connect**.
3. The app reports when the device cannot provide a selected profile; it does not substitute a lower resolution.

The first build has only been compile-verified. Camera/encoder combinations, actual 60 fps delivery, decoder transform availability, firewall behavior, image orientation, latency, and thermal behavior still need device testing.
