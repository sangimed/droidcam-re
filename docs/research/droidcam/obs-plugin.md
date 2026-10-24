# DroidCam OBS plugin 2.5.1

## Version boundary

The official installer is `DroidCamOBS.Setup.2.5.1.exe`; it was extracted statically with 7-Zip and not executed. The public plugin tag `2.5.1` points to `ddfa8880451f088e19b0358635922cf000bcda6d`, same as `master` at check time. Public source claims are attributed to that commit; exact binary/source identity is unproven.

Static extraction identifies the installer as NSIS 3. Its plugin DLL at `$_16_/obs-plugins/64bit/droidcam-obs.dll` is x64, 2,157,568 bytes, SHA-256 `D52DE0D405F7DF799324626D43D511FEEF9870F10F5629F196174D76107BDE56`. This differs from the client-bundled DLL (2,756,608 bytes, SHA-256 `9B4518C4F6E8B0F66D927AD7C9D2178999430DBD48B8F94437C0A4EE2F52BC48`). Static hashes establish separate builds; exact code divergence is not established.

## Source facts

- `src/source.cc` creates video decoder paths for H.264 (`AV_CODEC_ID_H264`), H.265 (`AV_CODEC_ID_H265`) and MJPEG. Decoder setup receives a `use_hw` choice for H.264/H.265; MJPEG uses a separate `MJpegDecoder`. Hardware acceleration defaults true in `source_defaults()`.
- `src/source.cc` configures OBS async unbuffered source output from the `OPT_UNBUFFERED_OUT` setting. `obs_data_set_default_bool(..., true)` sets the default to unbuffered in this commit. This controls OBS source output buffering; it does not prove the full capture-to-render path contains no queues.
- `src/ffmpeg_decode.cc` orders Windows hardware device attempts as D3D11VA, CUDA, then DXVA2 when the selected codec reports support. Decoder setup requests `AV_CODEC_FLAG_LOW_DELAY`, `AV_CODEC_FLAG2_FAST`, and slice threading. A decoded hardware frame is transferred with `av_hwframe_transfer_data()` to a regular AVFrame before its data pointers are assigned to the OBS frame. **INFERRED** — this introduces a GPU-to-host frame transfer in that implementation, so the source does not demonstrate an end-to-end GPU-resident path.
- In `source.cc::read_frame`, the client reads a fixed 12-byte header: 64-bit big-endian PTS followed by 32-bit big-endian payload length. A `NO_PTS` PTS marks a configuration packet, which is read and prepended to the next media packet. A media payload length of zero or above `MAXPACKET` ends the receive path; reads use `net_recv_all`. Video setup sends a `/video` request containing codec name, dimensions, USB port, client/OBS identity and HDR flag. Source `video_decode_thread` stamps decoded output with `data_packet->pts * 1000` and calls `obs_source_output_video2`.
- `src/decoder.h` has distinct receive and decode queues backed by deques and recycled `DataPacket` objects. The generic container itself has no fixed cap. In `ffmpeg_decode.cc::FFMpegDecoder::push_ready_packet`, the decoder enters catch-up after decode queue depth exceeds 30 for H.264/H.265. It then feeds into decode only after the backlog reaches at most one and an AVC IDR or HEVC IRAP/keyframe-class packet appears; intervening compressed packets are returned to the reusable receive pool. This is an explicit freshness/recovery policy and a potential visible frame skip. `mjpeg_decode.cc::MJpegDecoder::push_ready_packet` returns new frames to the reusable pool while decode queue depth exceeds one.
- `src/ffmpeg_decode.cc` and `src/mjpeg_decode.cc` are the decode implementations; `src/net.cc` and `src/device_discovery.cc` cover networking and discovery.

**INFERRED** — separate receive/decode and OBS output threads avoid doing all work on one callback, while the codec catch-up branch trades continuity for freshness after decode falls far behind. The threshold of 30 packets is a packet-count trigger, not a fixed millisecond latency bound; measured packet cadence and runtime behavior are required to quantify it.

## Source lines to preserve for review

Checkout root: `%TEMP%\droidcam-public-source-refs\obs-plugin`.

- `src/source.cc`: frame parsing `read_frame()` around 182–255; decode output around 260–290; codec selection around 300–365; receive/request loop around 395–500; unbuffered output setup near 860 and 1310; default near 1430.
- `src/decoder.h`: pooled packet structures and receive/decode deques, around lines 9–105.
- `src/ffmpeg_decode.cc`: decode catch-up/drop policy around lines 326–368; hardware/codec setup elsewhere in the same file.
- `src/mjpeg_decode.cc`: MJPEG decode and queue policy, around lines 48–54.
- `src/net.cc`, `src/device_discovery.cc`, `src/mdns_discovery.cc`: transport and discovery.

The separate public virtual-output repo `dev47apps/droidcam-obs-virtual-output` tag `0.2.2` / commit `5737685d455c9d0322feb01bf8ef053a7a2730ae` is a distinct downstream OBS output module. Its `src/plugin.cc::on_video` converts to shared-memory output; `src/structs.h` declares mapping/event names and framing structures; `src/yuv420_yuyv.c` performs the pixel conversion. This path is not the plugin's input socket or decoder path.

Virtual-output source locations: `src/plugin.cc` shared-memory mapping/events around 457–475, frame handoff around 487–507; `src/structs.h` mapping names and shared header structures; `src/yuv420_yuyv.c::map_yuv420_yuyv()` conversion implementation.

These line locations come from a temporary checkout and may shift if upstream changes; the commit is recorded above.
