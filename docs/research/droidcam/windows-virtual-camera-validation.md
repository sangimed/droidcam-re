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
