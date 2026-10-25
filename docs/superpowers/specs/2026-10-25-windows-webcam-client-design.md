# Windows webcam client — design

Date: 2026-10-25. Status: draft for owner approval after builder check.

## Intent and evidence

Turn the existing Android-to-Windows preview prototype into a usable video webcam client. The owner approved the scope in the grilling and selected the Windows 11 Media Foundation approach. Approval of this written design is still pending.

The current C++20 receiver already accepts clone-protocol TCP control and H.264/RTP video, decodes through Media Foundation, and displays BGRA through GDI. Android uses Camera2 and MediaCodec. Tablet E2E evidence is recorded in `../../research/droidcam/tablet-e2e-2026-10-25.md`. That evidence validates the existing stream, not the new virtual camera or discovery.

DroidCam is a functional reference, not an interoperability target. `../../research/droidcam/windows-client.md` distinguishes binary/package evidence from unknown runtime behavior. We do not reproduce proprietary code or claim unobserved DroidCam behavior.

## Agreed product requirements

- Windows 11 x64 minimum, build 22000; Android retains the current API 23 minimum and capability-based profiles. Other Windows architectures and Windows 10 are outside this milestone.
- Video only. No microphone, audio driver, or audio synchronization.
- One active Android camera per PC. A second device cannot preempt an active session.
- The virtual webcam must work in OBS and Discord simultaneously. This is a release gate, not an assumed API property.
- Android discovers available PCs and the user selects the destination. Manual IPv4 entry and retry remain available when discovery fails.
- A new device requires Windows acceptance and confirmation of the same association code on both screens. Remember authorized devices and support revocation from Windows.
- Automatically reconnect the same authorized device after an involuntary interruption. Explicit disconnect on either endpoint cancels reconnect until a new user action.
- Provide a Windows preview, clear connection state, connected device, received profile, disconnect command, and separate diagnostic panel.
- Hide stale video when the session ends or becomes stalled. Both preview and virtual output show an explicit neutral state instead of an apparently live frozen image.
- Once connected, video is available to webcam consumers without a separate start-output button. Use a stable device identity/name across client restarts and updates.
- While the client is running without a stream, the webcam delivers neutral video marked “Caméra déconnectée”; during recovery, “Reconnexion en cours”.
- Closing the main window exits the client, ends the session, cancels retries, and stops video production. No tray or background service workflow.
- Camera/profile controls stay on Android. Remote zoom, torch, camera switching, recording, USB transport, and internet/NAT traversal are outside this milestone.
- Supply guided installation and clean uninstall, including virtual-camera registration and necessary Private-network firewall configuration. Administrator permission is acceptable when needed.

## Approaches considered

1. **Chosen: native Windows 11 Media Foundation virtual camera.** Keep the C++/Media Foundation receiver and add a software camera media source. Limits the supported OS rather than introducing a legacy driver path. OBS/Discord compatibility and concurrent consumption must be demonstrated first.
2. **Not chosen: a legacy-compatible camera output/driver path.** Could broaden OS coverage, but introduces additional implementation, compatibility, and distribution work without an owner requirement for Windows 10.

Microsoft documents `MFCreateVirtualCamera` from Windows build 22000 and provides a virtual-camera sample. These establish an available mechanism, not application compatibility or a complete implementation.

- https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera
- https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera

## Architecture proposals made by the implementer

These are engineering choices for approval, not decisions attributed to the owner.

Keep the native Win32 shell, adding standard controls and explicit state management rather than migrating the existing receiver to Qt or embedding OBS. Preserve existing capture/encode/receive freshness behavior. Extract orchestration from `windows/src/main.cpp` as needed into focused session, UI, and output modules.

Use a Media Foundation COM media source registered by the installer. A bounded, latest-frame shared-memory bridge connects the receiver process to the media source hosted outside that process. Use OS access controls and a versioned layout; do not let a slow consumer block reception or another consumer. Shared memory has a stream generation and freshness timestamp so a producer crash cannot leave a live-looking old frame. The source generates neutral frames when the producer is absent or stale, including when consumer applications remain open after the client exits. Closing the client stops real-video production; the persistent camera registration remains until uninstall.

Start with a stable output contract of 1920×1080 at 30 fps, NV12, with aspect-preserving scaling/letterboxing from supported input profiles. A 60 fps input is sampled to 30 fps output; lower cadence repeats the freshest valid frame only within the freshness deadline. Output 60 fps and additional advertised media types are deferred unless the initial compatibility investigation proves another type is required. Input selection remains unchanged and is displayed separately from output format. This fixed contract avoids disruptive format renegotiation for consumers already running.

Use mDNS/DNS-SD advertisement `_droidcam-re._tcp.local.` on the Windows Private LAN and Android NSD browsing. Advertise PC name, stable installation ID, protocol version and control port, never credentials. Discovery is an untrusted hint: authentication must establish identity independently of names, IP addresses or advertisements. Handle duplicate names, changed addresses, removed services and multiple network interfaces. Discovery failure must not prevent manual connection. Do not advertise or add firewall rules for public-network access.

Introduce a versioned authenticated session protocol, separately from discovery. Prefer platform TLS and durable per-install identities with an established certificate-pinning enrollment pattern; do not invent cryptography. The displayed short code must be derived from the authenticated enrollment transcript, expire, and be rate limited. Both users must confirm the matching code before trust is persisted or camera video is accepted. Android uses Keystore-backed storage where supported; Windows uses OS-protected per-user credential storage. Revocation terminates the current session and rejects the old identity until explicitly paired again. Discovery and manual IP paths use the same enrollment checks. The implementation plan must select a concrete platform-compatible TLS/enrollment mechanism before production implementation; a code-only UI over unauthenticated protocol v1 is not an acceptable substitute. Video remains trusted-LAN RTP for this milestone; pairing does not claim video encryption or protect against a hostile LAN packet injector. That limitation must be documented accurately.

Use the current 750 ms RTP-silence threshold for a stalled state; also detect absent fresh decoded frames, even while packets keep arriving. Proposal: after 750 ms without a fresh decoded frame, mask live video and output neutral frames. A new session generation clears pending preview/output frames and codec state. Only a fresh decoded frame from the current generation restores live state. Recovering output must not expose a stale frame queued before disconnect.

Reconnect on Android while its foreground activity remains available. Retry with bounded exponential delay (1, 2, 4, 8, then 10 seconds), cancelled on explicit disconnect, permission failure, revocation, or owner rejection. Rediscover the same pinned PC identity after an address change. No Android background streaming/service is added. Closing Windows stops its advertisement/listener; a still-active Android sender may wait for its return after an involuntary loss, but a Windows Disconnect command explicitly instructs Android to stop retries.

## States and user behavior

Represent waiting, pairing, connecting, live, interrupted/reconnecting, explicitly disconnected, and actionable error states. New-device enrollment is available only when no active camera exists; reject concurrent attempts as busy without changing the active stream. Pending association has an expiry and cancel path. An authorized sender still cannot displace another active device.

The main window presents normal user information; technical counters and HRESULTs belong in diagnostics. Waiting shows the PC's discoverable name and fallback address. Failures distinguish permission denial, pairing rejection/expiry, device busy, network interruption, and unavailable camera output. The output can fail independently of the preview: report that failure explicitly and never imply the webcam is ready merely because preview works.

Exact visual layout is left to a later mockup review. This design specifies behavior and controls, not an approved visual mockup.

## Delivery sequence and decision gates

1. Validate an isolated virtual-camera path using synthetic frames, then receiver frames, in OBS and Discord concurrently. Exercise producer exit and restart with both consumers open. Record actual application versions, formats, and results. If it fails, investigate before committing to the rest of the output integration; do not silently remove simultaneous use from scope.
2. Implement lifecycle/state management, safe neutral output, stable camera identity and a usable Windows shell with diagnostics separated.
3. Implement authenticated enrollment, trust/revocation and reconnect semantics, then discovery on both endpoints. Test manual fallback with the same identity checks.
4. Package installation/update/uninstall and complete grouped device/application E2E. Update issue #4 as code lands.

Each subsystem will receive its own implementation plan with explicit interfaces and verification. Existing dirty worktree changes must be preserved and separately accounted for; this design does not authorize sweeping commits of unrelated changes.

## Acceptance and verification

- OBS and Discord enumerate the same persistent webcam and consume live video simultaneously without blocking each other. Closing either consumer does not stop the other.
- With consumers open, connect, interrupt the network, restore it, disconnect explicitly, restart the client and restart Android. Verify state transitions and absence of stale-frame presentation across generations.
- Verify synthetic/neutral output without Android, current input profile versus fixed output contract, scaling/aspect ratio, and approximately 30 fps output on the available device combination. Do not promise cross-device performance without evidence.
- A new device cannot send accepted video before matching-code confirmation. Rejected, expired, revoked and replayed enrollment attempts do not gain trust; a second device cannot take over a live session.
- Discovery finds multiple named PCs, handles duplicate names and changed addresses; manual fallback works when multicast is blocked. Public interfaces do not become advertised/allowed merely because installation succeeded.
- Permission denial then reauthorization and camera-only interruption remain grouped E2E items from issue #4. Existing Android unit tests and Windows CTest remain regression gates; add meaningful state/protocol/output tests as each subsystem is implemented.
- Installation, update and uninstall leave no duplicate devices or stale app firewall rules; upgrade preserves identity/trust, uninstall removes registration and stored credentials. Camera consumers already running during uninstall receive a clean stream end, not a blocked application.
- The owner's qualitative latency acceptance applies to the tested tablet baseline. The new output path needs a fresh visual latency check. Numeric glass-to-glass latency is unmeasured. Long battery/thermal testing remains waived; do not restart it as a release gate.

## Remaining engineering work

The first output investigation must settle Media Foundation source activation, IPC hosting/security, exact registration lifetime and consumer compatibility. The authenticated-session plan must pin down certificate bootstrap/code confirmation, Android API 23 compatibility, wire records and credential rotation/recovery. Library/API choices, exact layouts, installer tool and meaningful test boundaries are delegated to the builder within these requirements. Any finding that changes agreed product behavior returns to the owner before implementation proceeds.
