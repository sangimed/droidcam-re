# Windows client 7.2.1

## Package and client composition

**CONFIRMED** — `DroidCam.Client.7.2.1.exe` is an NSIS 3 installer packaged in an x86 Windows PE launcher (SHA-256 `5b0f8bf8bb68ace490cc5ad2172616ff3a61ae57121311289a4f4ecdf099dbb2`). The main extracted `bin/64bit/droidcam.exe` is x64, reports file version 7.2.1, and has a valid Authenticode signature. The extraction includes `obs.dll`, `libobs-d3d11.dll`, `libobs-opengl.dll`, Qt5 DLLs, FFmpeg 58/56/7 series DLLs and the DroidCam OBS/virtual-output plugins. This supports “OBS-based desktop client” at the packaging level. It does not prove which renderer or decoder is active in a session.

The client plugin `droidcam-obs.dll` is 2,756,608 bytes, SHA-256 `9B4518C4F6E8B0F66D927AD7C9D2178999430DBD48B8F94437C0A4EE2F52BC48`. The companion `droidcam-virtual-output.dll` is 1,164,800 bytes, SHA-256 `BDD45F99DC68BE441F4E20A0AA9DCDAD604A5A5DA71151D56D0EC7188E935789`. They differ from the plugin DLL bundled in the standalone OBS 2.5.1 installer (see [OBS plugin](obs-plugin.md)).

## Webcam output package

The client contains nested `DroidCam.Drivers.7.1.2.exe`, itself NSIS 3; it was also extracted statically. Its INF files describe `DroidCam Video` as a kernel capture device (`droidvcam2_vid0`) and `DroidCam Audio` as a kernel audio device (`droidvcam0_aud0`), with `NTamd64` and `NTarm64` targets. Driver files and INF service details prove a kernel driver is packaged; driver behavior is opaque here without runtime or native-driver analysis.

The public client source branch/tag `droidcam-7.2.1` at commit `d49f36986d17a359f52d3631833dc4cbcc754680` has DroidCam-specific OBS frontend overrides. In `UI/window-basic-main-outputs.cpp`, the customized frontend creates output ID `droidcam_virtual_output`; source-path evidence is not binary equality proof.

The public virtual-output plugin source tag `0.2.2` (`5737685d455c9d0322feb01bf8ef053a7a2730ae`) writes frames through named Windows shared-memory mappings (`DroidCamOBS_VideoOut1`) coordinated by read/write events. It configures an I420 output conversion, then `on_video()` converts I420 to YUYV and writes to the mapping. If the reader event is not acquired within 5 ms, it logs a dropped frame; thus this boundary favors keeping the producer moving over waiting indefinitely for the virtual-camera consumer. This describes public source, not necessarily the exact client DLL build.

## Remaining unknowns

Receiver thread topology and protocol on the embedded client, exact buffer limits and timestamp synchronization, selected FFmpeg hardware decoder/renderer at runtime, preview presentation path, plugin/driver IPC compatibility, and reconnection behavior remain unverified. No installer or extracted executable was run.

## Follow-up analysis

Static extraction and PE inventory are complete. Ghidra 12.1.x is not configured; Java 17 x64 is present, but REA requires an extracted Ghidra directory and matching native decompiler. No installation was attempted. `droidcam.exe`, plugin DLLs and driver remain unexecuted. The inbound client receive/decode path is not reconstructed from shipped binaries. Public 7.2.1 OBS client source is a candidate reference only; binary/source identity must be checked before equating the two.

## Evidence boundary

**CONFIRMED** — the installer embeds an OBS/FFmpeg-based x64 executable, an OBS source plugin, a virtual-output plugin and a nested kernel-driver package. The public virtual-output source describes a shared-memory/event handoff and I420→YUYV conversion, but this is a different output path from Android stream reception. Receiver protocol, buffer queues, chosen renderer and virtual camera driver runtime behavior remain **UNKNOWN** for the Windows standalone package.
