# DroidCam clone: implementation roadmap

This roadmap applies to a new, independent DroidCam-compatible product implemented in this repository. It is a separate product and does not reuse proprietary code. The repository currently contains the reverse-engineering documentation and official artifacts only; no Android or Windows application source exists yet.

Every DroidCam-specific claim below is bounded by the evidence in the linked research files. Unverified behavior is a design question for the clone, not a feature to assume.

## Five highest-value work items

1. **P0 — build the selected vertical slice.** Create an Android sender and Windows receiver using the clone-owned protocol (TCP control plus RTP/H.264 over UDP), frame telemetry, and local preview. Target 1920×1080 at 60 fps and provide 1920×1080 at 30 fps when supported; report device capability and do not silently change resolution. **Gain:** expose integration and latency risks early. **Cost/risk:** requires profile negotiation and a test harness before webcam-driver integration. **Validation:** stream a known scene at both profiles, measure actual FPS and frame age, disconnect/reconnect, and verify sustained operation.
2. **P0 — build pipeline observability into the protocol and both apps.** Track frame sequence IDs, monotonic stage times, queue depth/age, and drop reasons at camera, encoder, transport, decoder, and presentation. DroidCam end-to-end latency remains unknown. **Gain:** identify the actual bottleneck rather than tuning by feel. **Cost/risk:** clock mapping and telemetry overhead. **Validation:** compare per-stage timing with an independent glass-to-glass measurement.
3. **P1 — use bounded queues with an explicit freshness policy.** The public OBS plugin uses codec-aware catch-up after H.264/H.265 decode backlog and drops queued MJPEG frames. **Principle:** keep live preview current under overload, recovering inter-frame streams at a valid keyframe. **Gain:** avoid unbounded stale preview. **Cost/risk:** visible frame skips and codec-specific recovery. **Validation:** inject decode and network stalls; verify bounded frame age and clean recovery.
4. **P1 — benchmark Android capture and encode paths before selecting one.** DroidCam exposes a camera mode flag and native encoder calls, but the camera API and native codec implementation are unknown. **Principle:** compare supported camera output paths, including direct surface input to a hardware encoder where available. **Gain:** potential reduction in CPU conversion/copies. **Cost/risk:** device compatibility, crop/orientation complexity, and uncertain thermal effect. **Validation:** compare latency, CPU/GPU load, energy, and temperature on representative phones.
5. **P2 — treat Windows webcam output as a separate integration boundary.** Public source shows hardware decode frames copied to host memory and an I420→YUYV virtual-output conversion. The shipped standalone client binaries differ from the standalone OBS plugin build. **Principle:** first deliver a desktop preview; then choose a supported virtual-camera output path and minimize conversions required by its contract. **Gain:** avoid premature driver work and redundant copies. **Cost/risk:** OS driver/API support and format compatibility. **Validation:** measure frame age, conversion cost, output cadence, and interoperability with target capture applications.

## Suggested development sequence

| Phase | Deliverable | Exit evidence |
|---|---|---|
| 0 — scope | Written MVP, protocol choice, supported Android/Windows versions, codec and output requirements | Reviewable requirements and failure cases |
| 1 — vertical slice | Android camera preview plus encoded stream to Windows preview over one transport | Reproducible stream, frame IDs, stage timing, reconnect behavior |
| 2 — freshness and resilience | Bounded queues, drop/recovery rules, transport-loss behavior | Stall/loss experiments show bounded frame age and valid codec recovery |
| 3 — quality and energy | Hardware/software codec comparison and adaptive profiles | Controlled latency, CPU/GPU, battery and thermal results |
| 4 — virtual webcam | Windows virtual-camera output integration | Verified format contract and compatibility with target apps |

This sequence is a proposal based on the current workspace contents and reverse-engineering evidence. Implementation should begin only after approval of [the vertical-slice design](../../superpowers/specs/2026-10-24-droidcam-clone-vertical-slice.md) and its implementation plan. No numerical performance gain is claimed.
