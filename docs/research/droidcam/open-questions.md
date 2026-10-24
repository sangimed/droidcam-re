# Open questions and investigation checklist

## Android 10.0

- Trace `DroidcamService.onStartCommand`, camera-controller construction, camera open/session configuration and teardown.
- Identify exact API mapping for `camera2_api` modes and all selected resolutions/FPS ranges.
- Establish whether preview and encoder share a camera session; identify surfaces, rotation and buffer queues.
- Locate encoder path (MediaCodec Java, JNI/native, or no compressed video in some modes); determine codec/format, input mode, bitrate, keyframe behavior and queue strategy.
- Trace `Server` startup, discovery, control protocol, socket ownership, media framing, audio/video transport and reconnection.
- Trace power lock acquisition/release; map screen-off behavior and foreground-service state transitions.
- Inspect `.so` exports, imports, ELF architecture and DEX JNI declarations/call sites without bypassing protection logic.
- Establish whether the installed modern APK behaves differently from the older Classic APK; keep versions separate.

## Windows 7.2.1 and OBS installer 2.5.1

- Determine installer format with static metadata/format tools; extract without launching.
- Hash and inventory installed DLL/EXE/driver files; identify architecture, imports, codec dependencies and signing metadata.
- Check available Ghidra/native providers; do not install a new engine before assessing prerequisites.
- Compare client 7.2.1 source tag `droidcam-7.2.1` against extracted binaries; identify reception, preview and virtual camera output paths.
- Extract OBS 2.5.1 and compare decoder/network/queue behavior with the public 2.5.1 source snapshot.
- Analyze virtual output queue and pixel conversion, including video shared-memory mapping and driver buffering.

## Cross-phase / runtime

- Confirm Android modern app and Windows 7.2.1 actually share a protocol; do not infer from product branding.
- Measure end-to-end latency and stage timing for Wi-Fi versus USB and OBS versus standalone client.
- Measure sustained CPU/GPU load, current draw, thermal trajectory and throttling under controlled profiles.
- Compare source/binary build versions and exact module hashes before attributing public source mechanisms to released installers.

## Resolved in this pass

- Android service listener default port and TCP accept path are established (`ev_7e0ea7c936eb83d19e1abbded00cf2306517b0d5f3b429a400e5d824917f2e86`, `ev_2b7cbc18b4b54e9f4aa80a85c628700489822c5484a90407d3234b21eaa9a19c`).
- Android video route compatibility (`/v4`, `/v5`), codec tokens, MJPEG height limit and requested send buffer are established (`ev_4e70b87346e4367dc30c3d37efb71b323492a80d0ad3f47bb3d1459c2f809fe8`, `ev_40912752e1d5c5339ea525a61376d34aff49faec48a0dce6dbc20d22ba2edeb4`, `ev_d4f4f5054a5cc2201264173bdf2663660a01ac4ff6b1c78c3a7e111a7439afee`).
- Java-to-native encoder method boundary and negotiated profile logging are established (`ev_667ef84ac0aa2b8d43a457e9316fb49fd411c438b96ffe956213b38e4159f639`, native declaration IDs listed in [environment.md](environment.md)).
- Client and OBS installers were statically extracted; actual client decoder/receiver internals remain unresolved.

These do not resolve native implementation or runtime timing and therefore remain separate from the open items above.
