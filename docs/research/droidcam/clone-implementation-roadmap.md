# DroidCam clone: implementation roadmap

This roadmap applies to a new, independent DroidCam-compatible product implemented in this repository. It is a separate product and does not reuse proprietary code.

## Current implementation status (2026-10-30)

- **Vertical slice:** Android sender and Windows receiver are implemented; the receiver decodes H.264 and displays a local preview. Android profiles, telemetry, and bounded/freshness-aware decode are in place.
- **Windows virtual camera:** Media Foundation source, registration probe, bounded shared-memory bridge, synthetic producer, and receiver-frame publishing are implemented. Elevated registration succeeded on the test host, Windows enumerates `DroidCam Video`, and all six Windows CTest targets pass.
- **Compatibility gate:** Discord camera preview has not yet been confirmed. OBS and simultaneous OBS/Discord consumption, restart/recovery with consumers open, and the complete failure-injection matrix remain unverified. Do not mark the virtual webcam milestone complete until these checks have evidence.
- **Next action:** In Discord, select `DroidCam Video` under User Settings → Voice & Video and confirm the private preview. Then complete the remaining Task 4 compatibility checks in the [Windows virtual-camera implementation plan](../superpowers/plans/2026-10-25-windows-virtual-camera.md). No camera broadcast was initiated during the current host test.

See the [Windows virtual-camera validation report](windows-virtual-camera-validation.md) for host details and registration diagnostics.

Every DroidCam-specific claim below is bounded by the evidence in the linked research files. Unverified behavior is a design question for the clone, not a feature to assume.

## Five highest-value work items

1. **P0 — build the selected vertical slice.** Create an Android sender and Windows receiver using the clone-owned protocol (TCP control plus RTP/H.264 over UDP), frame telemetry, and local preview. Offer 1280×720 and 1920×1080 at 30/60 fps when supported; default to 1080p30 when available and show the selected profile. Resolution changes are explicit. **Gain:** expose integration, latency, and thermal risks early. **Cost/risk:** requires profile negotiation and a test harness before webcam-driver integration. **Validation:** stream a known scene at each profile, measure actual FPS and frame age, disconnect/reconnect, and verify sustained operation.
2. **P0 — build pipeline observability into the protocol and both apps.** Track frame sequence IDs, monotonic stage times, queue depth/age, and drop reasons at camera, encoder, transport, decoder, and presentation. DroidCam end-to-end latency remains unknown. **Gain:** identify the actual bottleneck rather than tuning by feel. **Cost/risk:** clock mapping and telemetry overhead. **Validation:** compare per-stage timing with an independent glass-to-glass measurement.
3. **P1 — use bounded queues with an explicit freshness policy.** The Windows prototype now uses a three-access-unit decode queue; on overflow it drops pending work, requests an IDR, and waits for a decodable recovery point. It also flags 750 ms of RTP silence and requests one recovery keyframe per stall. The public OBS plugin uses codec-aware catch-up after H.264/H.265 decode backlog and drops queued MJPEG frames. **Gain:** avoid unbounded application-level stale preview. **Cost/risk:** visible frame skips and codec-specific recovery. **Validation pending:** inject decode and network stalls; verify bounded frame age and clean recovery.
4. **P1 — benchmark Android capture and encode paths before selecting one.** The app now reports Camera2 capture cadence/frame number separately from encoder cadence, process CPU per core, battery temperature, and Android thermal status. It also lists each reported surface-input H.264 encoder per profile so runs can compare explicitly selected codecs without silent substitution. **Principle:** compare supported camera output paths, including direct surface input to a hardware encoder where available. **Gain:** potential reduction in CPU conversion/copies. **Cost/risk:** device compatibility, crop/orientation complexity, and uncertain thermal effect. **Validation pending in [grouped E2E issue #4](https://github.com/sangimed/droidcam-re/issues/4):** compare codecs, latency, CPU/GPU load, energy, and temperature on representative phones.
5. **P2 — treat Windows webcam output as a separate integration boundary.** Public source shows hardware decode frames copied to host memory and an I420→YUYV virtual-output conversion. The shipped standalone client binaries differ from the standalone OBS plugin build. **Principle:** first deliver a desktop preview; then choose a supported virtual-camera output path and minimize conversions required by its contract. **Gain:** avoid premature driver work and redundant copies. **Cost/risk:** OS driver/API support and format compatibility. **Validation:** measure frame age, conversion cost, output cadence, and interoperability with target capture applications.

## Suggested development sequence

| Phase | Deliverable | Exit evidence |
|---|---|---|
| 0 — scope | **Complete** — approved vertical-slice and Windows webcam designs | Requirements and failure cases recorded in the specs |
| 1 — vertical slice | **Implemented** — Android camera preview streams to the Windows receiver and preview | End-to-end stream and receiver telemetry documented; see the plan and E2E records |
| 2 — freshness and resilience | **Implemented; runtime fault matrix pending** — bounded queues, drop/recovery rules, and transport-loss handling | Unit tests pass; grouped injected-stall/loss and recovery checks remain pending |
| 3 — quality and energy | **Partially implemented; benchmark pending** — capture/encode telemetry and selectable profiles | Controlled codec, latency, CPU/GPU, battery, and thermal comparison remains pending in issue #4 |
| 4 — virtual webcam | **Implemented; compatibility gate pending** — Windows virtual-camera output is wired to receiver frames | Registration/enumeration and six CTest targets pass; Discord preview, OBS, concurrent consumers, and live recovery still need observed validation |

The Android sender now includes an explicit lower-resolution 720p profile and reversible black-screen DIM overlay based on observed DroidCam behavior. This is a mitigation to validate, not evidence that the clone has reached DroidCam's sustained thermal efficiency. See [thermal analysis](thermal-analysis.md) and grouped [E2E issue #4](https://github.com/sangimed/droidcam-re/issues/4).

The implementation status above is based on workspace and host-test evidence. The remaining validation items are explicit gates, not assumed passes. No numerical performance gain is claimed.
