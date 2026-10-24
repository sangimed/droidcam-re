# DroidCam Clone Vertical Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a new Android sender and Windows preview receiver that stream 1920×1080 H.264 at 60 fps when supported and offer 30 fps at the same resolution.

**Architecture:** Android Camera2 feeds a MediaCodec H.264 encoder using an encoder input Surface plus a preview Surface. A versioned TCP session channel negotiates profile/control, while an RTP/H.264 UDP stream carries video to a native Windows receiver using Media Foundation decode and a minimal D3D11 preview. The clone owns this protocol; it will not interoperate with official DroidCam apps.

**Tech Stack:** Kotlin, Android Gradle Plugin, Camera2, MediaCodec, CMake, native Windows C++, Winsock, Media Foundation, Direct3D 11.

**Spec:** `docs/superpowers/specs/2026-10-24-droidcam-clone-vertical-slice.md`

## Global Constraints

- Implement only the new independent clone in this repository; do not modify official artifacts.
- Do not copy or redistribute decompiled/proprietary DroidCam code.
- Android minSdk is 23, matching the studied modern DroidCam package; compileSdk and targetSdk are 36, matching the studied target API.
- Target 1920×1080 at 60 fps and support a 1920×1080 at 30 fps profile when hardware allows.
- Do not silently substitute 720p; report unsupported profile combinations.
- Use TCP for versioned session/control and RTP/H.264 over UDP for video.
- Begin with manual IPv4 peer entry on a trusted local LAN; no mDNS, USB/ADB, audio, pairing, or webcam driver in this milestone.
- Keep application queues bounded and report packet/frame loss and queue age.
- Do not add or run automated tests without the user's request; use the specified builds and manual acceptance observations only after implementation is approved.

## Review Focus

- Unsupported 1080p60: expose 1080p30 if supported and never silently lower resolution.
- Lost/reordered/late RTP fragments: discard incomplete access units and request a keyframe without feeding partial data to the decoder.
- Malformed or oversized control messages: reject safely before allocation or session startup.
- Hardware decoder unavailable: use a software fallback if available; otherwise surface a clear error.
- Peer disconnect or stalled receiver: bound queues and release camera, codec, sockets, and renderer resources without replaying stale frames.

---

## File structure

- `android/settings.gradle.kts`, `android/build.gradle.kts`, `android/gradle.properties`, `android/gradlew*`: Android project and reproducible Gradle entry point.
- `android/app/src/main/AndroidManifest.xml`: camera/network permissions and launcher activity.
- `android/app/src/main/java/dev/droidcamclone/app/MainActivity.kt`: profile selection, preview surface, peer address, start/stop and status UI.
- `android/app/src/main/java/dev/droidcamclone/camera/CameraController.kt`: Camera2 capability query, preview/encoder surface session, orientation and lifecycle.
- `android/app/src/main/java/dev/droidcamclone/codec/H264Encoder.kt`: MediaCodec configuration, output format/config units, keyframe request and release.
- `android/app/src/main/java/dev/droidcamclone/net/SessionClient.kt`: versioned TCP control handshake and keyframe request handling.
- `android/app/src/main/java/dev/droidcamclone/net/RtpH264Packetizer.kt`: H.264 access-unit parsing and RFC 6184 RTP packetization.
- `android/app/src/main/java/dev/droidcamclone/telemetry/FrameStats.kt`: stage timing, FPS, queue-age/drop counters.
- `windows/CMakeLists.txt`: native Windows build targets and system libraries.
- `windows/src/main.cpp`: process entry and lifecycle. `windows/src/window.h`, `window.cpp`: minimal window, peer/session lifecycle, visible selected mode and diagnostics.
- `windows/src/net/session_server.h`, `windows/src/net/session_server.cpp`: bounded TCP handshake/control listener on port 48200 by default and UDP port negotiation.
- `windows/src/net/rtp_h264_receiver.*`: RTP sequence tracking, SSRC validation, FU-A reassembly deadline, access-unit completion and drop accounting.
- `windows/src/video/mf_h264_decoder.*`: Media Foundation H.264 MFT setup, input/output sample flow and hardware/software selection.
- `windows/src/video/d3d11_preview.*`: D3D11 device/swapchain and decoded-frame presentation.
- `windows/src/telemetry/frame_stats.*`: receive/decode/present timing and queue/drop metrics.
- `docs/research/droidcam/clone-protocol.md`: version 1 handshake fields, RTP profile, limits and failure behavior shared by both platforms.

## Task 1: Scaffold both platform builds and the shared protocol contract

**Files:** See project files above, `docs/research/droidcam/clone-protocol.md`.

**Interfaces:** Protocol v1 defines a bounded session handshake: protocol version, session ID, chosen 1920×1080 profile (30 or 60 fps), H.264 codec/profile, peer UDP port, RTP payload type and SSRC. Control TCP listens on port 48200 by default (configurable); sender advertises an ephemeral UDP port. Messages use network byte order and are limited to 4096 bytes. RTP UDP payloads are capped at 1200 bytes including the RTP header.

- [ ] Create an Android Gradle app and Windows CMake executable skeleton.
- [ ] Write protocol v1 field layout and explicit maximum control-message length; implement matching serialization/parsing entry points in each platform.
- [ ] Configure Android with minSdk 23, compileSdk 36, targetSdk 36, and a compatible Gradle wrapper. Configure the Windows x64 target with the available MSVC/Windows SDK toolchain; environment inventory currently shows none on PATH.
- [ ] Build the empty Android app with Gradle wrapper and the Windows skeleton with CMake/MSVC; record exact toolchain requirements and commands in the project README.

## Task 2: Android 1080p capture, preview and H.264 sender

**Files:** `MainActivity.kt`, `AndroidManifest.xml`, `CameraController.kt`, `H264Encoder.kt`, `SessionClient.kt`, `RtpH264Packetizer.kt`, `FrameStats.kt`.

**Interfaces:**
- `CameraController.queryProfiles(): List<VideoProfile>` returns only 1920×1080 profiles supported by both camera output configuration and an H.264 encoder.
- `VideoProfile(width: Int, height: Int, fps: Int, bitrate: Int, codecMime: String)` represents one negotiated mode; fps is 30 or 60.
- `H264Encoder.start(profile: VideoProfile, inputSurface: Surface, listener: OutputListener)` and `requestKeyFrame()` / `stop()` own codec lifecycle.
- `RtpH264Packetizer.packetize(accessUnit: ByteBuffer, ptsUs: Long): List<ByteBuffer>` emits RTP datagrams with a monotonic sequence number, 90 kHz timestamp mapping, access-unit marker, and correct single-NAL/FU-A packets.

- [ ] Add profile enumeration. Offer 1080p60 and 1080p30 only when camera and encoder capabilities support each; never return 720p as a silent fallback.
- [ ] Create a Camera2 session with matching preview and encoder surfaces; report unsupported simultaneous surface/profile combinations to UI.
- [ ] Configure MediaCodec H.264 encoder for the selected FPS and a bounded bitrate. Preserve codec config and PTS; add a bounded output handoff to the packetizer.
- [ ] Implement RTP v2/H.264 single NAL and FU-A packetization, correct sequence/timestamp/marker behavior, and UDP send pacing that does not queue stale access units.
- [ ] Add foreground-only start/stop lifecycle for this MVP; release camera, codec and sockets on stop, permission denial, and connection failure.
- [ ] Build the Android debug APK and inspect logs for the advertised profiles and clean startup/teardown.

## Task 3: Windows session, RTP receiver and H.264 decode/preview

**Files:** `windows/CMakeLists.txt`, `windows/src/main.cpp`, `windows/src/window.h`, `windows/src/window.cpp`, `windows/src/net/session_server.h`, `windows/src/net/session_server.cpp`, `windows/src/net/rtp_h264_receiver.h`, `windows/src/net/rtp_h264_receiver.cpp`, `windows/src/video/mf_h264_decoder.h`, `windows/src/video/mf_h264_decoder.cpp`, `windows/src/video/d3d11_preview.h`, `windows/src/video/d3d11_preview.cpp`, `windows/src/telemetry/frame_stats.h`, `windows/src/telemetry/frame_stats.cpp`.

**Interfaces:**
- `SessionServer` parses the protocol v1 handshake and returns a selected profile or bounded error.
- `RtpH264Receiver` emits only complete ordered access units before a configured reassembly deadline; incomplete, duplicate, wrong-SSRC, late, or oversized units increment a reason-specific drop counter.
- `MfH264Decoder.submit(accessUnit, pts90k)` queues compressed samples and invokes a decoded-frame callback with presentation time.
- `D3D11Preview.present(frame)` presents the newest decoded frame without unbounded render queuing.

- [ ] Implement TCP handshake parsing, UDP socket setup, capability/profile agreement and keyframe-request control messages.
- [ ] Implement RTP header parsing and H.264 FU-A reassembly with bounded datagram/frame sizes, sequence/loss tracking and expiration of incomplete access units.
- [ ] Initialize Media Foundation H.264 decoder with hardware acceleration where supported and a clear software fallback or unsupported status.
- [ ] Create a D3D11 preview window and present decoded frames; cap pending frame work to favor freshness.
- [ ] Implement session teardown for peer disconnect, malformed datagrams, window close and decoder failure; keep allocations bounded by protocol limits.
- [ ] Build the Windows x64 executable and verify dependency/toolchain requirements are documented.

## Task 4: End-to-end telemetry, integration and manual acceptance

**Files:** `FrameStats.kt`, `frame_stats.*`, Android and Windows status UI, `docs/research/droidcam/clone-protocol.md`, `docs/research/droidcam/latency-analysis.md`, `docs/research/droidcam/thermal-analysis.md`.

- [ ] Add sequence IDs and monotonic local timestamps at encoder output, packet send/receive, decode output and presentation submission. Keep phone and PC clocks separate unless synchronized.
- [ ] Show selected 1080p profile, actual FPS, decoder mode, packet loss/reordering, dropped frames and queue age.
- [ ] On incomplete access unit, discard it; request a keyframe over TCP and wait for a decodable recovery point.
- [ ] Exercise manual-LAN session startup, both 1080p profiles where available, peer disconnect/reconnect, receiver stall and injected packet loss; record observations without adding automated tests.
- [ ] Run a sustained capable-device 1080p60 session and a 1080p30 comparison; record latency percentiles, delivered FPS, CPU/GPU/codec mode, battery/current and temperature trend.
- [ ] Document any device profile that cannot provide 1080p60; keep resolution at 1080p and report unsupported rather than downscaling.

## Out of scope

Audio, mDNS discovery, USB/ADB forwarding, Android background streaming, official DroidCam interoperability, multi-camera controls, user pairing/security UX, and Windows virtual-camera output remain follow-up phases.
