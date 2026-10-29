# Windows Virtual Camera Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the first approved subsystem: a stable Windows virtual webcam consumed simultaneously by OBS and Discord, initially synthetic and then fed by the existing receiver.

**Architecture:** A persistent Media Foundation software camera registration activates a COM media-source DLL in the Windows camera host. A bounded shared-memory bridge carries only the newest receiver frame to independent consumers; absent or stale producers yield neutral frames. Preserve the current receiver protocol and GDI preview while verifying this output boundary.

**Tech Stack:** C++20, Win32, Media Foundation, Windows SDK, CMake/MSVC, existing CTest infrastructure.

**Spec:** `docs/superpowers/specs/2026-10-25-windows-webcam-client-design.md` (approved).

## Global Constraints

- Windows 11 x64 minimum, build 22000; Android retains the current API 23 minimum and capability-based profiles.
- Video only. No microphone, audio driver, or audio synchronization.
- The virtual webcam must work in OBS and Discord simultaneously.
- One active Android camera per PC. Preserve current bounded reception/decoding and IDR recovery.
- Output contract: 1920×1080 at 30 fps, NV12, aspect-preserving scaling/letterboxing. Do not advertise unsupported types.
- A new frame generation invalidates all old-generation images. After 750 ms without a fresh decoded frame, emit neutral frames.
- Closing Windows stops real-video production; the persistent registration stays until uninstall. No tray or producer service.
- Long battery/thermal testing remains waived. Existing qualitative tablet latency acceptance does not validate this new output path.
- Preserve and account for the existing dirty worktree; no sweeping commits. Use an isolated execution context carrying the actual current baseline, not an older HEAD that omits existing source changes.
- No copied proprietary DroidCam code. Refer to Microsoft documentation/sample for API contracts and verify license before incorporating sample code.

## Review Focus

1. FrameServer account/session access to IPC: an ordinary-user producer must work without exposing frames to unrelated users (Task 2 integration test).
2. Producer dies during a write: consumers must neither block nor display old video indefinitely (Tasks 1–2).
3. One consumer stalls or exits: the other continues with independent request/sample timing (Tasks 3–4).
4. Consumer stays open across producer restart: generation and timestamp reset must not resurrect old frames (Tasks 1, 4–5).
5. Camera privacy denies activation or registration fails: fail visibly and clean partial registration without claiming webcam readiness (Tasks 3, 5).

## File structure and execution order

Create `windows/src/virtual_camera/` for `frame_contract.h`, `frame_bridge.h/.cpp`, `frame_conversion.h/.cpp`, `media_source.h/.cpp`, `media_stream.h/.cpp`, `dllmain.cpp`, `virtual_camera.def`, and `registration.h/.cpp`. Keep COM source lifetime and streaming state out of receiver `main.cpp`.

Create `windows/tools/virtual_camera_probe.cpp` for registration, removal and synthetic production, and `windows/tests/virtual_camera_{frame,bridge,source}_test.cpp` for contract/conversion, IPC and COM source tests. Modify `windows/CMakeLists.txt` to build the source DLL, probe, shared conversion/bridge library and test targets. Add `docs/research/droidcam/windows-virtual-camera-validation.md` for actual observations and `tools/test-virtual-camera.ps1` for bounded host checks.

Tasks 1–3 establish synthetic output. **Task 4 is a mandatory compatibility gate before receiver integration in Task 5.** Each task ends with a focused commit of its files; partial edits in shared existing files must be staged by hunk or isolated from prior work.

### Task 1: Output format, freshness and generation contract

**Files:** Create `frame_contract.h`, `frame_conversion.h/.cpp`, `windows/tests/virtual_camera_frame_test.cpp`; modify `windows/CMakeLists.txt`.

**Interfaces:** Namespace `clone_vcam`. Define `FrameState { Disconnected, Reconnecting, Live }`; `FrameMetadata { uint64_t generation, sequence, capturedQpc; FrameState state; }`; `OutputFrame { FrameMetadata metadata; std::vector<uint8_t> nv12; }`. `ConvertBgraToOutput(std::span<const uint8_t> bgra, uint32_t width, uint32_t height, uint32_t stride, OutputFrame& output) -> bool`; `IsFresh(const FrameMetadata&, uint64_t currentGeneration, uint64_t nowQpc, uint64_t qpcFrequency) -> bool`. Fixed output buffer size is 3,110,400 bytes. Timestamp output samples in 100 ns units using a rational 30 fps accumulator, not rounded per-frame integer milliseconds.

- [ ] Write failing tests `rejectsInvalidDimensionsAndStride` (including overflow/truncated buffer), `letterboxes720pAndPortrait`, `convertsKnownBlackWhiteNv12`, `generationRejectsOldFrames`, and `freshnessDeadline`. Assert fresh at 749 ms, stale at 750 ms; future timestamps fail closed. Assert cumulative timestamps at 30 frames equal 10,000,000 ticks and strictly increase.
- [ ] Configure with `cmake -S windows -B windows/build -A x64`; build `cmake --build windows/build --config Release --target droidcam_clone_vcam_frame_tests`. Confirm missing implementation fails before implementing it.
- [ ] Implement the interfaces and deterministic neutral-image generation with French state text. Use checked bounds and aspect-preserving conversion; do not change the existing decoder output contract.
- [ ] Run `ctest --test-dir windows/build -C Release -R virtual_camera_frame --output-on-failure`; require all assertions passing.
- [ ] Commit only this task's changes with message `feat: define virtual camera frame contract`.

### Task 2: Bounded cross-process latest-frame bridge

**Files:** Create `frame_bridge.h/.cpp`, `windows/tests/virtual_camera_bridge_test.cpp`; modify `windows/CMakeLists.txt`.

**Interfaces:** Consume Task 1 types. `FramePublisher::Open(const std::wstring& installationId) -> HRESULT`; `FramePublisher::Publish(const OutputFrame&) -> bool`; `FramePublisher::Close() -> void`. `FrameReader::Open(const std::wstring& installationId) -> HRESULT`; `FrameReader::ReadLatest(uint64_t nowQpc, OutputFrame&) -> bool`. Each reader has independent local sample state; producer never waits for reader consumption. `ReadLatest` returns false on incomplete, inaccessible or incompatible mapping and caller supplies neutral output.

- [ ] Write failing tests `twoReadersIndependent`, `rejectsLayoutVersionAndLength`, `producerCrashMidWrite`, `restartChangesGeneration`, and `unauthorizedUserDenied`. Assert bounded read retries, no torn accepted frame, no blocking lock abandoned by a dead process, and stale generation rejection. Publish 300 frames while one reader never reads; the second receives the newest frame and memory remains bounded.
- [ ] Build target `droidcam_clone_vcam_bridge_tests`; confirm failure before implementation.
- [ ] Implement a versioned fixed-capacity mapping with publication sequence/slot validation using documented Windows cross-process synchronization primitives. Use a random installation identity and explicit ACL for the producer user and actual camera-host principals; record the verified host identities, rather than granting Everyone access. Separate producer-write and consumer-read rights. Enforce layout size/version and fixed output bounds before copying. Keep handles under RAII.
- [ ] Run `ctest --test-dir windows/build -C Release -R virtual_camera_bridge --output-on-failure`. Then exercise publisher under ordinary user and reader in actual FrameServer hosting in Task 3; record ACL/namespace behavior. If platform hosting prevents isolation, resolve the IPC boundary before proceeding.
- [ ] Commit `feat: add bounded virtual camera frame bridge`.

### Task 3: Media Foundation source and registration probe

**Files:** Create `media_source.h/.cpp`, `media_stream.h/.cpp`, `dllmain.cpp`, `virtual_camera.def`, `registration.h/.cpp`, `windows/tools/virtual_camera_probe.cpp`, `windows/tests/virtual_camera_source_test.cpp`; modify `windows/CMakeLists.txt`.

**Interfaces:** Consume `FrameReader` and Task 1 contract. Implement COM activation through one permanent source CLSID, frozen at first implementation and documented in the validation report; class factory and DLL lifetime follow COM rules. Implement `IMFMediaSource`/`IMFMediaStream` and required activation/service interfaces per the Microsoft software-camera contract. `VirtualCameraRegistration::EnsureInstalled() -> HRESULT`, `Start() -> HRESULT`, `Stop() -> void`, `Remove() -> HRESULT`. Friendly name is `DroidCam RE`; let Windows append its documented virtual-camera suffix. The installation ID and source CLSID remain stable across upgrades.

- [ ] Add failing tests `oneNv12Stream1080p30`, `invalidMediaTypeRejected`, `requestSamplePreservesToken`, `stopShutdownUnblocksRequests`, `independentConsumerClocks`, and `missingProducerGeneratesNeutral`. Assert sample dimensions/length, timestamps/durations and event order; reject calls after shutdown with the documented HRESULT.
- [ ] Build target `droidcam_clone_vcam_source_tests` and confirm failure before implementation.
- [ ] Implement activation, presentation/stream descriptors, asynchronous events, requested-sample delivery, start/stop/shutdown and balanced COM/resource lifetime. Read newest valid bridge frames on sample production, not a backlog. Never perform an unbounded consumer wait inside a COM event/sample callback.
- [ ] Implement the probe modes `--register`, `--remove`, `--synthetic`, `--stop`, with nonzero exit and readable HRESULT on failure. Register a system-lifetime camera for the current user; COM source registration may need elevation and must resolve in the host process. Keep camera registration distinct from producer lifetime. Link `mfsensorgroup` and required SDK libraries in the dedicated targets. Do not run elevated registration implicitly from a build/test.
- [ ] Run `ctest --test-dir windows/build -C Release -R virtual_camera_source --output-on-failure`; verify registration idempotence, camera privacy denial, ordinary-user streaming after installation, source activation and unregister cleanup on the actual host. Record any permissions needed.
- [ ] Commit `feat: expose Media Foundation virtual camera source`.

### Task 4: Simultaneous OBS/Discord compatibility gate

**Files:** Create `tools/test-virtual-camera.ps1`, `docs/research/droidcam/windows-virtual-camera-validation.md`. Adjust Task 3 implementation only when actual failures justify changes.

**Interfaces:** Probe command line from Task 3; no Android dependency for this gate. The helper accepts `-ProbePath`, `-OutputDirectory` and a bounded `-DurationSeconds` (default 30), captures process failures and registration diagnostics, and does not report application success without observed evidence.

- [ ] Build `droidcam_clone_virtual_camera` and `droidcam_clone_vcam_probe` in Release and verify the DLL/probe target artifacts exist. Register with the exact required privileges established in Task 3.
- [ ] Produce a synthetic moving frame counter. Open the source in OBS and Discord concurrently; record application versions, actual negotiated types, visible advancing counters and any permission prompts. If login or human interaction is needed, ask for that concrete action while continuing independent checks; never fabricate a pass.
- [ ] Stop consuming in OBS, then Discord, in turn; the remaining consumer must keep advancing. Kill synthetic producer mid-write, restart it, and keep consumers open: neutral output must replace stale video and new-generation video must recover without re-adding the source. Test idle activation, source shutdown and privacy-denied activation.
- [ ] Record pass/fail for each case, build hashes and the tested Windows build. Update issue #4 with results and any required follow-up. A failure blocks Task 5 until diagnosed; changing concurrent-consumer scope requires owner agreement.
- [ ] Commit `test: document concurrent virtual camera compatibility` with observed evidence only.

### Task 5: Feed receiver frames without weakening freshness

**Files:** Modify `windows/src/main.cpp`, `windows/CMakeLists.txt`; create `windows/src/virtual_camera/receiver_output.h/.cpp`; extend `windows/tests/virtual_camera_frame_test.cpp` and validation report.

**Interfaces:** Consume `clone_video::DecodedFrame` from `windows/src/video/h264_decoder.h`, Task 1 conversion and Task 2 publisher. `ReceiverOutput::Start(const std::wstring& installationId) -> HRESULT`; `BeginSession(uint64_t generation) -> void`; `OnDecodedFrame(const clone_video::DecodedFrame&, uint64_t generation, uint64_t nowQpc) -> void`; `SetState(FrameState) -> void`; `Stop() -> void`. A new generation resets cached data before any frame can be published; reject callbacks with a mismatched generation. Use the receiver-local decoded arrival time, never subtract Android and PC clocks.

- [ ] Write failing tests `disconnectInvalidatesPublishedFrame`, `oldCallbackCannotPublishIntoNewSession`, `conversionFailureEmitsNeutral`, and `slowOutputDoesNotBlockDecode`. Include disconnect with a queued UI/frame callback. Assert output errors are observable independently of preview status.
- [ ] Run the owning CTest target and confirm failure before implementation.
- [ ] Implement a bounded newest-frame output worker; at most one pending decoded frame plus the frame being processed. Do not convert/copy 1080p images synchronously under the decoder or UI locks. Begin/end output generations at actual session boundaries; mask both `displayedFrame` and `latestFrame` on disconnect/stall. Cancel old-generation UI notifications. Report unavailable virtual output as a distinct status without stopping an otherwise functional preview.
- [ ] Re-run all Windows tests with `ctest --test-dir windows/build -C Release --output-on-failure`. Stream the benchmark Android app to both OBS and Discord concurrently; verify live output, 720p input scaling, 1080p input, DIM continuity, network interruption, explicit disconnect and Windows restart with consumers open. Visually recheck latency. Test 60 fps input only if device capabilities allow, recording unsupported otherwise.
- [ ] Update the validation report and issue #4, then commit `feat: publish receiver video to virtual webcam` using focused staging that preserves prior `main.cpp` changes.

## Self-review and continuation

This plan covers the approved design's first output gate and receiver integration only. It intentionally does not implement discovery, authenticated enrollment, full interface, or installer distribution. Registration in the probe is an engineering tool, not the delivered installer. Tests distinguish source/bridge correctness from real application compatibility.

After the output gate produces evidence, prepare and approve the next focused plans:

1. **Windows interface and lifecycle:** explicit UI/session model, diagnostics panel, device/trust controls and clean exit. Preserve the output interfaces above. Mockup review precedes committing to exact layout.
2. **Authenticated connectivity and delivery:** concrete TLS/enrollment protocol and cross-platform fixtures first; trust/revocation and explicit-close semantics; mDNS/Android NSD with manual fallback; foreground reconnect delays 1/2/4/8/10 seconds; installer/update/uninstall and grouped E2E. Split this further if enrollment and packaging warrant independent execution plans. No secure-association claim until the protocol is selected, reviewed and tested on API 23-compatible APIs.

All approved product requirements remain in scope through these successor plans. Do not treat completion of this file as completion of the overall Windows-client milestone.
