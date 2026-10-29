# Thermal and energy analysis

## Measurement correction: debug runtime overhead

The phone had been running a **debuggable clone APK**, while the installed DroidCam APK was non-debuggable. A 15-second `simpleperf` capture found CheckJNI and the debug ART interpreter on the clone's hot paths, including UDP sends and codec callbacks. CheckJNI frames disappeared in a non-debuggable, shell-profileable build of the same code. Android explicitly advises against [measuring performance with debug builds](https://developer.android.com/topic/performance/measuring-performance), and documents [CheckJNI activation for debuggable applications](https://developer.android.com/ndk/guides/jni-tips#extended_checking).

With **camera 0, 1920×1080, fixed 30 fps, the same hardware H.264 encoder, codec-default High profile and DIM**, three 10-second process CPU samples were:

| Build | CPU percent of one core | Mean | Measured camera-to-encoder stream cadence |
|---|---|---|---|
| Debug | 57.47, 46.43, 37.53 | 47.14% | 30.00 fps |
| Non-debuggable benchmark | 18.44, 21.85, 21.34 | 20.54% | 30.00 fps |
| Non-debuggable benchmark, repeat after final diagnostic metadata update | 41.86, 45.72, 43.67 | 43.75% | 30.00 fps |

The first pair showed about 56% less process CPU time, but **the repeat did not reproduce that reduction**. It must not be presented as a stable gain or thermal fix. The debug checks are a confirmed source of extra work, but camera/ISP, hardware codec, radio, display, DVFS, compilation state and starting temperature also matter. The short first benchmark warm-up reached AP 52.0°C / skin 39.5°C; later readings during the same stream were AP 45.4°C / skin 38.7°C. These are a trajectory, not a matched thermal A/B result.

### Counter-based verification at the original target

Three subsequent 10-second hardware-counter samples per build, with camera 0, 1920×1080, 30 fps and DIM, gave:

| Metric | Debug | Non-debuggable benchmark |
|---|---|---|
| Mean process CPU / one core | 54.63% | 44.92% |
| Measured mean clock from cycles/task time | 0.724 GHz | 0.735 GHz |
| Instructions per image at 30 fps | 5.944 million | 5.130 million |
| CPU cycles per image at 30 fps | 13.191 million | 11.005 million |

This run measured **13.7% fewer instructions and 16.6% fewer CPU cycles per image** in the benchmark build. Camera timestamps confirmed 29.999 fps at 1920×1080; the preview stream remained paused in DIM. These app-process counters support removing debug overhead, but do not measure total device energy. The debug simpleperf helper emitted child-command warnings while returning complete event counters; those warnings are recorded as a measurement limitation. The earlier apparent 56% improvement is not used as an acceptance result.

The counter run began with skin 40.6°C / battery 37.2°C and ended at skin 40.3°C / battery 37.2°C, with thermal status MODERATE. **The phone still becomes warm**; a one-hour matched thermal run remains open. Use [the measurement script](../../../tools/measure-android-stream.ps1), which rejects a debug APK unless explicitly allowed, and records raw counters, clock estimates, camera ID, cadence and available temperatures. Raw probe summaries are preserved in [runtime evidence](runtime-thermal-2026-10-25.json).

The `benchmark` build type inherits release behavior, is non-debuggable and shell-profileable, and uses the local debug certificate for installation. CI builds both debug and benchmark APKs. CSV exports include the debuggable flag and camera ID. The default is restored to **1080p30**, and exact fixed FPS ranges are preferred when reported by Camera2. The optional 720p profile is not the thermal correction.

## Comparison controls and rejected experiments

- Earlier DroidCam samples used **camera 2** (2.2 mm focal length, 5.6×4.2 mm sensor metadata), whereas the clone used **camera 0** (6.7 mm, 9.6×7.2 mm). Both report 4000×3000 active arrays. Those samples compared different physical cameras as well as different build types.
- After selecting camera 0 in DroidCam, its 720p visible-preview process samples were 24.83%, 24.73%, 23.47%; with DIM, 20.52%, 19.13%, 19.31%. The latter run also sampled the hardware-codec service at 5.91–6.41%, camera provider at 25.17–27.72%, and cameraserver at 7.49–8.09% of one core. These services must be considered separately from app process CPU.
- `dumpsys media.resource_manager` confirms **both apps use `c2.exynos.h264.encoder` as a hardware-video-codec resource**. DroidCam's codec logs show AVC Baseline, a configured 60 fps encoder rate, a two-second keyframe interval in its own startup log, and runtime bitrate updates from 4 Mbit/s to 3.6, 3.2, then 2.8 Mbit/s. Its observed camera cadence on the low-light scene was approximately 16.64 fps with an AE range of 8–30. The bitrate adaptation algorithm remains unknown.
- DroidCam camera 0 uses one 720p PRIVATE output with a `SurfaceTexture` consumer. The clone normally configures separate SurfaceView and encoder outputs. A trial that reconfigured DIM to a single encoder output did not show a material sustained reduction in the sampled app/camera/codec services; it was **reverted**. DIM continues to remove the preview target from repeating requests without rebuilding the capture session.
- A supported AVC Baseline request was tested in isolation. At 720p/16.64 fps it gave a small, noisy process CPU difference; at 1080p30 its mean was 51.59%, versus 47.14% with the codec-default High profile. The Baseline change was **reverted**, because it did not establish a benefit at the original target profile.

## Established facts and limits

- **CONFIRMED, runtime comparison on the connected Samsung phone (2026-10-25)** — the installed DroidCam app was streaming AVC at 1280×720 with AE FPS range 8–30. Its `DIM` control made the screen black while its process, Camera2 session, partial wake lock, and TCP stream to the Windows client remained active. Over separate 10-second samples its process used about 18–23.5% of one core; reported AP temperature moved from 46.0°C to 44.5°C and skin from 40.1°C to 39.1°C. Thermal status remained MODERATE.
- **CONFIRMED, clone runtime sample (not controlled against the DroidCam run)** — the clone had been streaming 1920×1080@30 through `c2.exynos.h264.encoder`, with roughly 49–60% of one core, AP 49.1°C, skin 40.9°C and battery 37.4°C. Starting temperatures, charging/radio state and measurement timing were not held constant, so these numbers do not prove a direct temperature or power advantage for DroidCam. The observed profile and DIM behavior are useful implementation targets.
- **CONFIRMED, clone source inspection** — before the DIM capture-target change, Camera2 sent every frame to both the on-device preview and the H.264 encoder. DIM now removes the preview surface from the repeating capture request while retaining the encoder target, then restores it on tap. The encoder output must be copied out of MediaCodec before its buffer is released; the old RTP packetizer also copied NAL units and temporary fragments. It now writes datagrams directly from ranges in the owned encoded frame, removing those intermediate NAL/fragment copies. Both are plausible CPU/memory-bandwidth reductions, not yet measured thermal fixes. DroidCam's equivalent internal path remains unknown.
- The clone offers an explicit 1280×720 profile and defaults to 1080p30 when available. The receiver accepts both sizes. A reversible black overlay dims the phone screen while keeping the preview surface attached and the stream active; the app also keeps the display awake during a session. Sustained comparison remains pending in [E2E issue #4](https://github.com/sangimed/droidcam-re/issues/4).

- **CONFIRMED** — Android manifest and service declarations permit foreground camera/microphone/connected-device use; the service creates Wi-Fi, optional multicast and wake locks during `onCreate()`. This is lifecycle/power-management behavior, not a power optimization claim.
- **UNKNOWN** — DroidCam's exact sensor readout mode, full image conversion/copy path, GPU usage and CPU/GPU duty cycle. Camera IDs, output consumers and the hardware encoder name are now observed, but no controlled sustained thermal/power comparison has established the remaining heat distribution.
- **CONFIRMED, public Windows OBS source** — the decode path attempts D3D11VA, CUDA and DXVA2 in order, then transfers a hardware-decoded frame to a regular AVFrame before OBS output. The virtual-output handoff converts I420 to YUYV using an SSE2 path where compiled. This does not establish Android thermal cost, but it identifies host-side GPU→CPU and pixel-conversion work that the clone should instrument if it adopts a similar webcam output design.
- `libmjpg.so` is present in the ARM64 split; the filename alone does not prove whether it encodes, decodes, or is called in the streaming path.

## Measurement plan for the clone

Use **benchmark or signed release APKs**, record the debuggable flag, and match the physical camera ID as well as resolution/FPS/codec/bitrate, screen brightness, radio conditions and charging state. Compare clone preview target enabled versus removed by DIM, capture FPS, encoder FPS, process/thread CPU, camera/codec service CPU, GPU utilization if available, encoder name, encoded bytes/sec, battery current/charge, skin/battery temperature and Android thermal status. Record baseline idle and warm-up trajectory, then compare one change at a time. The DIM UI alone is not evidence that camera/preview rendering work is reduced.

To separate algorithm load from radio/thermal effects, compare local capture+encode without transport, encode+loopback transport, and full remote receive/render. Repeat at multiple ambient/device starting temperatures. The goal is to identify which stage contributes work and whether throttling changes latency over time.

**INFERRED** — hardware surface input to a hardware encoder can reduce CPU copies and conversion cost where supported, but may increase device-specific GPU/display contention or produce different thermal characteristics. Test on target phones; do not treat “hardware” as inherently cooler or lower latency.

**CONFIRMED Android boundary** — the app negotiates resolution and logs bitrate/FPS/keyframe interval-related fields before native encoder initialization, and asks for a 256 KiB TCP send buffer. This establishes available profile/socket controls but does not show actual encoder type, queue occupancy, CPU/GPU use or power impact. Evidence `ev_667ef84ac0aa2b8d43a457e9316fb49fd411c438b96ffe956213b38e4159f639`, `ev_d4f4f5054a5cc2201264173bdf2663660a01ac4ff6b1c78c3a7e111a7439afee`.
