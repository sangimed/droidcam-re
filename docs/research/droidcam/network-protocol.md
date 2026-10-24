# Network protocol and transport

## Android modern app

**CONFIRMED** — manifest requests INTERNET and Wi-Fi/network permissions; `DroidcamService.onCreate()` registers a Wi-Fi network callback, optionally creates a multicast lock based on `allow_wifi_nsd`, and the class summary contains `core.Server` and a method accepting `java.net.Socket`. Evidence: `ev_6255da16a238f949e30723ed75a64b08198f518870c273e7bd8ae48a97707416`, `ev_8d96a5b083d27cf6f09e5db5820a4b52bb2de61e80b6d299f334d275d314585d`, `ev_3ff8f64d4afc1272420fbe5f6063171a81d7ef30b64504dedc0527bf837ae912`.

**CONFIRMED** — native `Server.CheckMDNS`, `ServiceMDNS` and `StopMDNS` declarations exist; `libmdns.so` exists in the ARM64 split. **INFERRED** — mDNS participates in discovery. Native loading and runtime call-path linkage are not yet demonstrated.

**UNKNOWN at initial class-inventory stage; refined below** — discovery records, handshake details, transport framing, audio/video multiplexing, reconnection/backoff and USB bridging. The Server implementation now confirms the video listener uses TCP, but that does not establish every media/control path.

## OBS plugin public source snapshot

At `dev47apps/droidcam-obs-plugin` commit `ddfa8880451f088e19b0358635922cf000bcda6d`, source paths include `src/device_discovery.cc`, `src/mdns_discovery.cc`, `src/net.cc`, `src/source.cc`, and `src/buffer_util.h`. This is a distinct client integration and must not be treated as proof of the modern Android protocol until matched against the Android implementation or a recorded session.

**CONFIRMED in public source commit 2.5.1** — video uses a TCP socket request for `/video`; request parameters include codec name, width, height, USB port, client/OBS version identifiers and HDR flag. `source.cc::read_frame()` reads a 12-byte header with 64-bit big-endian PTS and 32-bit big-endian payload length, then reads the declared bytes using `net_recv_all`. A special `NO_PTS` denotes a codec configuration packet buffered and prepended to the next frame. `net_recv_all` blocks until full field/payload or timeout/error. Config/media interpretation is source-level; exact Android producer implementation remains unverified.

`device_discovery.cc` handles Android USB/ADB and iOS USBMux connection variants; `mdns_discovery.cc` handles network discovery. The plugin's route is therefore not simply “same Wi-Fi TCP” in all cases, but source alone does not establish equivalence with current Android app or Windows standalone. Installer identity has not been validated.

The request calls `set_recv_buf_len(sock, 65536 * 4)` before receiving video. This is the configured OS socket receive buffer request, not a decoded-frame FIFO size and not evidence of a 256 KiB media backlog in normal operation.

## Public Linux client boundary

The Linux client source at `cdc044bd74873c6b8750750aac42db8029dac5c1` is a separate implementation and can serve as protocol corroboration, not as proof of Windows/Android 10.0 parity. Its socket/queue implementation is listed in `environment.md`; exact packet flow remains under analysis.

## Android route and socket evidence (app 10.0)

**CONFIRMED** — `DroidcamService` starts `core.Server` on preference `app_port` (default 4747). `Server.run()` opens a TCP `ServerSocket`, polls accept with 64 ms timeout, enables keep-alive on accepted sockets, and routes video streams into a worker. Evidence `ev_7e0ea7c936eb83d19e1abbded00cf2306517b0d5f3b429a400e5d824917f2e86`, `ev_2b7cbc18b4b54e9f4aa80a85c628700489822c5484a90407d3234b21eaa9a19c`.

**CONFIRMED** — Android route dispatch accepts GET `/v5/video/...` and `/v4/video/...`; `/v1` through `/v3` are marked as legacy. Codec token mapping is `avc`→1, `jpg`→2, `hevc`→3. Video setup parses requested width×height; MJPEG mode rejects heights over 1080 and requests `SO_SNDBUF=262144`. This capacity request is not proof of actual queued media or a fixed delay. Evidence `ev_4e70b87346e4367dc30c3d37efb71b323492a80d0ad3f47bb3d1459c2f809fe8`, `ev_40912752e1d5c5339ea525a61376d34aff49faec48a0dce6dbc20d22ba2edeb4`, `ev_d4f4f5054a5cc2201264173bdf2663660a01ac4ff6b1c78c3a7e111a7439afee`.

**CONFIRMED, OBS source only** — plugin 2.5.1 requests `/v5/video/...`; parser reads a 12-byte big-endian PTS/length header and then the payload. Android accepts `/v5`, but the exact producer framing remains **UNKNOWN** until native output or a capture is correlated. Do not generalize this framing to `/v4`, standalone Windows, or the old Classic APK without evidence.

**UNKNOWN** — exact mDNS service record, discovery handshake, USB forwarding details, audio wire framing, reconnect/backoff, and whether the modern `/v5` Android path emitted by this APK is byte-for-byte the OBS source parser's expected format. The Java HTTP-like route/control dispatcher is statically demonstrated; runtime interoperability still needs validation.
