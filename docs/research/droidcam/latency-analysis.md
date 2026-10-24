# Latency analysis

## What is established

- **CONFIRMED** — Android service exposes camera mode setting and owns a server/socket boundary, but no capture-to-transmit method chain is yet established.
- **CONFIRMED, public OBS source** — the OBS plugin supports unbuffered async source output, default true; it also uses a catch-up policy after the H.264/H.265 decode backlog exceeds 30 packets, then resumes at a qualifying keyframe, and drops queued MJPEG arrivals when the decode queue already exceeds one. This demonstrates deliberate freshness handling at the plugin decoder/output boundary, not measured end-to-end latency.
- **CONFIRMED, public source** — the virtual-output handoff uses a shared-memory writer and a reader event wait limited to 5 ms; missing the lock causes frame drop. This makes that boundary bounded in wait time by design, with freshness favored over guaranteed delivery. It is not a 5 ms end-to-end latency value.
- **CONFIRMED, public source** — hardware decode output is copied from the hardware frame to a host AVFrame before it is handed to OBS. The virtual-output plugin later converts I420 to YUYV (SIMD SSE2 on x86 builds where available). These are identifiable copy/conversion stages to measure in a comparable clone path.
- **UNKNOWN** — any DroidCam end-to-end latency number, Android encoder delay, network queue depth, Windows decoder/render latency or difference USB vs Wi-Fi. No runtime measurements were performed.

## Clone measurement design

Add monotonic timestamps and a frame sequence number at each boundary:

| Stage | Timestamp |
|---|---|
| Camera callback / buffer available | `t_capture` |
| Encoder input queued | `t_enc_in` |
| Encoded output available | `t_enc_out` |
| Last transport byte received | `t_rx` |
| Decode output available | `t_dec_out` |
| Render submission / presentation | `t_present` |

Compute stage intervals on one device with a monotonic clock; for cross-device one-way network timing, synchronize clocks or use a visual LED/photodiode method rather than subtracting unsynchronized wall clocks. Track p50/p95/p99, dropped/reordered frames and queue depth/oldest-frame age. Measure glass-to-glass with a high-speed camera or display+phone stimulus and report the method.

Use a matrix that holds scene, resolution, frame rate, codec, bitrate and device thermal starting state constant while comparing Wi-Fi versus USB and hardware versus software decode. Record packet loss/retransmits and network conditions. A/B single buffering changes to avoid attributing multiple simultaneous changes.

## Queue hypotheses

**INFERRED** — the OBS source's catch-up behavior indicates that keeping every compressed frame is less valuable than recovering a current decodable picture after a severe backlog. Prefer bounded queues with explicit freshness policy for preview. For inter-frame codecs, discard policy must respect keyframe/reference dependencies; drop to a recovery keyframe or use codec-aware decode rather than arbitrary byte/frame dropping. Note this implementation may skip visible frames and is not evidence that it lowers sustained thermal load.

## Android path constraints

**CONFIRMED** — the Android listener requests `SO_SNDBUF=262144` for video sockets. This is a socket capacity setting only; occupancy, queue age and latency impact are **UNKNOWN**. The app negotiates AVC/MJPEG/HEVC and calls a native encoder init method after parsing stream parameters, but native encode delay and producer backpressure are not visible in Java. Evidence `ev_d4f4f5054a5cc2201264173bdf2663660a01ac4ff6b1c78c3a7e111a7439afee`, `ev_667ef84ac0aa2b8d43a457e9316fb49fd411c438b96ffe956213b38e4159f639`.

The `/v5` route aligns between Android dispatch and the OBS 2.5.1 request. Only the OBS source side establishes the 12-byte PTS/length parsing; Android packet framing is **UNKNOWN** pending native analysis or a passive session capture.
