# DroidCam Clone

An independent Android-to-Windows camera stream prototype. This project is not wire-compatible with DroidCam's official apps and contains no proprietary DroidCam code.

## Current target

- Android sender and Windows preview receiver.
- H.264 at 1280×720 or 1920×1080, targeting 30/60 fps when the camera and encoder support the selected profile; defaults to 1080p30 when available.
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

## LAN and Windows firewall

Use this prototype only between devices on a trusted local network. Do not create a public-network firewall exception, router port-forward, or internet-facing listener.

1. Connect the phone and PC to the same non-guest Wi-Fi/LAN. Guest Wi-Fi and client-isolation settings can prevent devices from reaching each other. Temporarily disconnect VPNs if they route local traffic away from the LAN.
2. On Windows, run `ipconfig` and use the PC's IPv4 address from the active Wi-Fi or Ethernet adapter in the Android app.
3. Start the receiver. If Windows Firewall prompts, allow `droidcam_clone.exe` on **Private networks only**. If the prompt was dismissed, add the receiver executable under **Windows Security → Firewall & network protection → Allow an app through firewall**, and enable only the Private profile.
4. The fixed control listener uses TCP `48200`. Video uses a UDP port selected dynamically for each session, so a port-only UDP rule cannot target a single fixed video port. If configuring firewall rules manually, scope the inbound rule to the receiver executable, the Private profile, and the local subnet.
5. Check that the active Windows network is classified as **Private** only when it is a trusted LAN. Do not switch an untrusted network to Private just to make the prototype connect.

For a local debug build, an administrator PowerShell can create an executable-scoped rule for the Private profile and local subnet:

```powershell
$receiverPath = (Resolve-Path ".\windows\build\Debug\droidcam_clone.exe").Path
New-NetFirewallRule -DisplayName "DroidCam Clone (Private LAN)" -Direction Inbound -Program $receiverPath -Action Allow -Profile Private -RemoteAddress LocalSubnet
```

To remove that rule later, run `Remove-NetFirewallRule -DisplayName "DroidCam Clone (Private LAN)"` in administrator PowerShell.

If Android reports a control connection timeout, first check the IPv4 address, Windows network profile, and TCP firewall rule. If the session is accepted but no video arrives, check whether the firewall permits the receiver executable's UDP traffic on the Private network. Keep the firewall profile and network topology in the E2E results.

## Build

Android debug APK:

```powershell
cd android
.\gradlew.bat assembleDebug
```

For latency, CPU and temperature measurements, use the non-debuggable benchmark APK. It follows the release configuration, allows shell profiling, and uses the local debug certificate so it can update a locally built debug installation:

```powershell
cd android
.\gradlew.bat assembleBenchmark
adb install -r app/build/outputs/apk/benchmark/app-benchmark.apk
```

Debug builds enable runtime checks and compiler restrictions that distort performance measurements. Use `benchmark` or a signed `release` build for sustained streaming comparisons. CI provides both APKs in the `android-apks` artifact.

With the benchmark app streaming on an Android 10+ phone, run this from the repository root to capture CPU time, hardware cycles, instructions, observed camera stream cadence and available temperatures:

```powershell
.\tools\measure-android-stream.ps1 -OutputPath "$env:TEMP\droidcam-performance.json"
```

The script rejects debug APKs unless `-AllowDebug` is explicitly passed for a control run. Compare instructions/cycles at matched resolution, FPS and camera ID; CPU percentages alone change with processor frequency.

Windows x64 debug preview:

```powershell
cmake -S windows -B windows/build -G "Visual Studio 17 2022" -A x64
cmake --build windows/build --config Debug
```

## Use

1. Start `windows/build/Debug/droidcam_clone.exe`.
2. On Android, enter the Windows PC's IPv4 address, choose an available profile (1080p30 is selected by default when supported), and tap **Connect**. Use **Dim** to black out the screen while keeping the stream active; tap anywhere to restore the controls.
3. The app reports when the device cannot provide a selected profile; it does not substitute a different resolution during a session.

Short device probes have confirmed 1080p30 on the connected SM-G998B. Sustained frame-rate delivery, other camera/encoder combinations, decoder availability, network recovery, latency and thermal acceptance remain tracked in [E2E issue #4](https://github.com/sangimed/droidcam-re/issues/4).
