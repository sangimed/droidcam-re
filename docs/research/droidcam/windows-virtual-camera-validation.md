# Windows Virtual Camera Validation Report

Date: 2026-10-29
OS: Windows 11 Pro x64 Build 26300 (10.0.26300)
Architecture: x64
Media Foundation Virtual Camera CLSID: `{C1B4DF19-9430-47FE-8356-9F7C2D323E70}`
Friendly Name: `DroidCam RE` (Windows displays as `DroidCam RE Windows Virtual Camera`)

## 1. Automated Test Suite Results

All 6 automated CTest targets pass (100% green):
- `nv12_to_bgra`: Passed (GDI preview color conversion)
- `freshness_queue`: Passed (bounded queue, stall and drop policies)
- `rtp_h264_receiver`: Passed (RTP assembly, sequence handling, loss detection)
- `virtual_camera_frame`: Passed (1080p30 NV12 conversion, 720p/portrait letterboxing, BT.709 colorimetry, 750 ms freshness deadline, exact 30 fps rational sample clock)
- `virtual_camera_bridge`: Passed (cross-process latest-frame shared memory, independent readers, crash resilience, epoch kernel event authentication, strict ACL for producer/system principals)
- `virtual_camera_source`: Passed (COM `IMFMediaSource`/`IMFMediaStream`, 1 NV12 stream descriptor, rejection of invalid media types, sample request token preservation, stop/shutdown unblocking, independent consumer clocks, automatic neutral frame fallback when producer is absent or stale)

## 2. Windows 11 Virtual Camera Registration Diagnostics

Execution of `tools/test-virtual-camera.ps1` under standard user permissions observed:
- `MFCreateVirtualCamera` succeeds with `S_OK` (verifying Windows 11 build 22000+ virtual camera API support).
- Calling `IMFVirtualCamera::Start` without elevation returns `0x80004002` (`E_NOINTERFACE`).
- Root cause: The Windows Camera Frame Server (`Windows.Devices.Mfp.FrameServer.exe`) runs under `NT AUTHORITY\LOCAL SERVICE`. It activates the registered Media Source COM CLSID from `HKEY_LOCAL_MACHINE\Software\Classes\CLSID\{C1B4DF19-9430-47FE-8356-9F7C2D323E70}`. Registration in `HKEY_CURRENT_USER` is inaccessible to `LOCAL SERVICE`.
- Writing to `HKLM` requires an elevated one-time registration command. Per project constraints, elevation is never triggered implicitly from automated scripts or builds.

### Registration Command (Administrator):
```powershell
Start-Process "windows\build\Release\droidcam_clone_vcam_probe.exe" -ArgumentList "--register" -Verb RunAs
```
Or via regsvr32:
```cmd
regsvr32 "D:\Repositories\droidcam-re\windows\build\Release\droidcam_clone_virtual_camera.dll"
```

## 3. Synthetic Producer Verification

The probe tool `windows\build\Release\droidcam_clone_vcam_probe.exe` provides `--synthetic` mode:
- Produces 1920×1080 @ 30 fps NV12 video with an animated moving test block.
- Publishes into the shared memory bridge with current QPC timestamps.
- Can run bounded (`--duration <seconds>`) or continuous until Ctrl+C.

## 4. OBS & Discord Concurrent Compatibility Status

Environment scan during testing:
- Discord: Process detected (`Discord.exe` running).
- OBS: Not currently running (`obs64.exe` absent).
- Actual application consumption gate requires the virtual camera registration (step 2) before selecting `DroidCam RE Windows Virtual Camera` in OBS Studio (Video Capture Device) and Discord (User Settings -> Voice & Video -> Camera).

## 5. Receiver Integration (2026-10-30)

- The Windows receiver now starts the shared-memory publisher when a registered installation ID is available. Failure is shown as a virtual-camera status while the H.264 preview remains available.
- Each accepted receiver session gets a monotonically increasing local generation. Session start publishes a reconnecting image immediately; decoded frames enter a bounded newest-frame worker using the same shared frame allocation as the GDI preview.
- Stream stall and disconnect publish neutral output and clear both queued and displayed preview frames. Stale callbacks from earlier generations are rejected.
- The Release application and virtual-camera frame test targets build successfully. All six CTest targets pass on this host.
- The synthetic probe opened the publisher and published 30 frames during a bounded two-second run.
- OBS/Discord live consumption of the receiver path, network interruption recovery, and restart with consumers open have not been exercised; the existing concurrent-consumer gate remains unverified.

## 6. Registration Error Follow-up (2026-10-30)

- After an elevated `--register` attempt returned `0x80004002`, a direct COM query against the registered CLSID reproduced the exact failure: `IMFMediaSource` succeeded while `IMFActivate` returned `E_NOINTERFACE`.
- The COM class factory had returned `MediaSource` directly for every requested interface. It now returns an `IMFActivate` implementation that creates and owns the media source on activation.
- The new `sourceClassFactoryProvidesActivate` regression test failed before the fix and passes after it. Direct COM queries now return `S_OK` for both interfaces.
- Frame Server validation also required `IMFMediaSourceEx`, `IKsControl`, and `IMFMediaStream2`, plus Frame Server stream attributes and matching stream identifiers. Those interfaces and attributes were added; the stream identifier mismatch found by `Start` (`MF_E_INVALIDSTREAMNUMBER`) was corrected and covered by regression assertions.
- The updated `--register` command was then run elevated and succeeded, returning installation ID `{C69715A4-C9F8-4248-8DF9-AE6725D78825}`. Windows enumerates the device as `DroidCam Video` (`ROOT\\MEDIA\\0004`, status `OK`).
- Discord camera preview is not yet verified. The UI showed the Voice & Video settings once but returned to the active channel during automated inspection. The camera was not turned on in the live voice channel, so no broadcast was initiated.
