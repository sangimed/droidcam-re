# Thermal and energy analysis

## Established facts and limits

- **CONFIRMED** — Android manifest and service declarations permit foreground camera/microphone/connected-device use; the service creates Wi-Fi, optional multicast and wake locks during `onCreate()`. This is lifecycle/power-management behavior, not a power optimization claim.
- **UNKNOWN** — camera sensor mode, frame rate, encoder hardware use, image conversion path, GPU usage, CPU/GPU duty cycle, device power draw and thermal throttling. No runtime profiling was performed.
- **CONFIRMED, public Windows OBS source** — the decode path attempts D3D11VA, CUDA and DXVA2 in order, then transfers a hardware-decoded frame to a regular AVFrame before OBS output. The virtual-output handoff converts I420 to YUYV using an SSE2 path where compiled. This does not establish Android thermal cost, but it identifies host-side GPU→CPU and pixel-conversion work that the clone should instrument if it adopts a similar webcam output design.
- `libmjpg.so` is present in the ARM64 split; the filename alone does not prove whether it encodes, decodes, or is called in the streaming path.

## Measurement plan for the clone

Run sustained sessions at controlled resolution/FPS/codec/bitrate with fixed screen brightness, radio conditions and charging state. Log per-stage frame times, CPU usage by thread, GPU utilization if available, codec implementation name, memory bandwidth/copies where measurable, battery current/charge, skin/battery temperature and Android thermal status. Record baseline idle and warm-up trajectory, then compare one change at a time.

To separate algorithm load from radio/thermal effects, compare local capture+encode without transport, encode+loopback transport, and full remote receive/render. Repeat at multiple ambient/device starting temperatures. The goal is to identify which stage contributes work and whether throttling changes latency over time.

**INFERRED** — hardware surface input to a hardware encoder can reduce CPU copies and conversion cost where supported, but may increase device-specific GPU/display contention or produce different thermal characteristics. Test on target phones; do not treat “hardware” as inherently cooler or lower latency.

**CONFIRMED Android boundary** — the app negotiates resolution and logs bitrate/FPS/keyframe interval-related fields before native encoder initialization, and asks for a 256 KiB TCP send buffer. This establishes available profile/socket controls but does not show actual encoder type, queue occupancy, CPU/GPU use or power impact. Evidence `ev_667ef84ac0aa2b8d43a457e9316fb49fd411c438b96ffe956213b38e4159f639`, `ev_d4f4f5054a5cc2201264173bdf2663660a01ac4ff6b1c78c3a7e111a7439afee`.
