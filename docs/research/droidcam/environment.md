# Environment and evidence ledger

## Tool status

- Host: Windows x64, PowerShell, workspace `D:\Repositories\droidcam-re`.
- REA CLI: `rea-agents@6.7.0` (invoked with `npx.cmd -y rea-agents@latest`; version reported by doctor).
- REA MCP: Codex registration in `C:\Users\revol\.codex\config.toml` is aligned, but this active Codex session did not expose REA tools. `doctor --client codex --json` reported `live_server.state=unknown`; use a fresh Codex session to reconnect and confirm `binary_session` / Android tools. CLI fallback is functioning.
- JADX MCP JAR: `%USERPROFILE%\.local\share\rea\android\jadx-headless-mcp-0.7.1-all.jar`, SHA-256 `6e5eacf500b64292bfb73c49797c1958f6ee44646e43e868039ae7feb573ff75` (matches expected). `REA_JADX_MCP_JAR` was absent initially; set for the current analysis process and persisted at Windows user scope. A newly started process sees the persisted value.
- Java: 17.0.14 reported by JADX. APK load reports 16,756 parsed classes and 1,012 resources.
- Ghidra: REA provider inventory says unavailable/not configured (`GHIDRA_INSTALL_DIR` absent); `ghidraRun.bat` was not on PATH. Host is Windows x64 and a 64-bit JDK 17.0.14 is present. REA indicates Ghidra 12.1.x as the required release family and expects an extracted installation with `support\analyzeHeadless` and a matching native decompiler; set `GHIDRA_INSTALL_DIR` to the install root, then recheck `rea providers`. No Ghidra or JDK installation was attempted. Android Java analysis does not require Ghidra.

The broad doctor environment status is not fully healthy because optional analysis providers are absent and other client registrations are stale. Codex registration and JADX checks passed after setting the variable. These unrelated registration findings were left untouched.

## Artifact inventory

| Artifact | Size | SHA-256 | Status |
|---|---:|---|---|
| `base.apk` | 25,674,737 | `e7e166a68e5b991853b031c79273f53e40d953013c2cc781b7248eaa4c9a6162` | inspected by REA JADX |
| `split_config.arm64_v8a.apk` | 2,519,701 | `7c2ca553adfaebd832e34ea8be5313de9354634a984de4949b25e38efbb96c0` | library inventory only |
| `split_config.en.apk` | 16,793 | `096518596118e9eb5902acb114a866db8c2408c23be767bc0a8858e11e9b5a88` | not inspected |
| `split_config.xxhdpi.apk` | 114,551 | `da5f8bbb8f2621c5f8b611a1fe594179342d3c89d0e3dda21c2cd45ebca6d6f0` | not inspected |
| `DroidCam.Client.7.2.1.exe` | 103,246,728 | `5b0f8bf8bb68ace490cc5ad2172616ff3a61ae57121311289a4f4ecdf099dbb2` | NSIS statically extracted |
| `DroidCamOBS.Setup.2.5.1.exe` | 19,703,296 | `fb20d899cd2aa45b3670e7ee10a5e90130dc489321770dd271b701d82522db31` | NSIS statically extracted |

The base APK SHA-256 was supplied in the investigation brief and reproduced by REA Evidence. Other digests were read from local files.

## Android Evidence IDs

| Evidence ID | Operation | Key scope |
|---|---|---|
| `ev_6255da16a238f949e30723ed75a64b08198f518870c273e7bd8ae48a97707416` | `inspect_android_package` | APK identity, manifest, permissions, services and activity declarations |
| `ev_3ff8f64d4afc1272420fbe5f6063171a81d7ef30b64504dedc0527bf837ae912` | `inspect_android_class` | `DroidcamService` fields/method inventory |
| `ev_6f05eaf31d6f6f29fef747f68dc6caa5392e0c4aebf9156eecfc2db7649554f5` | `inspect_android_class` | `MainActivity` inventory |
| `ev_8d96a5b083d27cf6f09e5db5820a4b52bb2de61e80b6d299f334d275d314585d` | `inspect_android_method` | `DroidcamService.onCreate()` decompilation |
| `ev_5b1bc686fcfa3d5efa309646ffc0f61cba43e36be7cd144c11a3130081c7e811` | `inspect_android_method` | `DroidcamService.onStartCommand()`; returns integer 1 |
| `ev_fd89574b171d750d1656620cf43681d3c1394a4f7fc8cb39fef7341d2eeb4699` | `inspect_android_method` | `DroidcamService.onDestroy()`; controller, locks, server and callbacks cleanup |
| `ev_4dc9ae4ffaccb185b47e00d2fbff7b082416f308c4009f19c22b6d863d1c065a` | `inspect_android_method` | `MainActivity.onCreate()` delegates via a reflective obfuscated bootstrap |
| `ev_446283cb809cfecba425d69453d4562a968f7774f086433d38e6597620802b95` | `inspect_android_class` | `com.dev47apps.obsdroidcam.core.Server` declarations |
| `ev_a6b77221c0da12af71c78588f97bf7bbaf28c18bb13561776387bb3db082f3a6` | `inspect_android_method` | `Server.CheckMDNS()` marked native |
| `ev_e87786f3d94d3896b078ba9829eb13e228ac5dd46ea70826b8091ecf5bb3997b` | `inspect_android_method` | `Server.ServiceMDNS(byte[],byte[],byte[],int)` marked native |
| `ev_3bb9230c106976db56a71ab735b7c7b80abc6b0ff3efbd10d2a0011b8d6f7bf4` | `inspect_android_method` | `Server.StopMDNS()` marked native |

Evidence limitations: static inspection does not establish runtime behavior; APK signatures and split APK composition were not verified by JADX; Android references may omit dynamic/reflection paths; no target was executed.

## Public source snapshots

Read-only checkouts were placed under `%TEMP%\droidcam-public-source-refs` (outside the repository):

| Repository | Branch/tag | Commit |
|---|---|---|
| `dev47apps/droidcam-obs-plugin` | `2.5.1` | `ddfa8880451f088e19b0358635922cf000bcda6d` |
| `dev47apps/droidcam-obs-client` | `droidcam-7.2.1` | `d49f36986d17a359f52d3631833dc4cbcc754680` |
| `dev47apps/droidcam-obs-virtual-output` | `0.2.2` | `5737685d455c9d0322feb01bf8ef053a7a2730ae` |
| `dev47apps/droidcam-linux-client` | current `master` on 2026-10-24 | `cdc044bd74873c6b8750750aac42db8029dac5c1` |

The OBS 2.5.1 tag matched `master` at the time checked. The other repositories are separate projects and their versions are not assumed to match either official installer.

## Static extraction outputs

7-Zip 24.09 identified both top-level installers as NSIS 3 PE launchers and extracted their files without running them. Outputs are in `%TEMP%\droidcam-extracted-static-202da283f2d34f8e91f46386d3eee88b\client-7.2.1-files` and `...\obs-2.5.1-files`; both are outside the repository. Client extraction contained 475 files / 321,455,024 uncompressed bytes; OBS setup contained 45 files / 82,116,791 uncompressed bytes.

Key extracted hashes: standalone OBS installer plugin `droidcam-obs.dll` = `D52DE0D405F7DF799324626D43D511FEEF9870F10F5629F196174D76107BDE56` (2,157,568 bytes); embedded client plugin `droidcam-obs.dll` = `9B4518C4F6E8B0F66D927AD7C9D2178999430DBD48B8F94437C0A4EE2F52BC48` (2,756,608 bytes); client `droidcam-virtual-output.dll` = `BDD45F99DC68BE441F4E20A0AA9DCDAD604A5A5DA71151D56D0EC7188E935789` (1,164,800 bytes). The plugin DLLs are not byte-identical; equivalence must not be assumed.

Additional Android method Evidence IDs captured during route and encoder tracing:

| Evidence ID | Operation | Key scope |
|---|---|---|
| `ev_7e0ea7c936eb83d19e1abbded00cf2306517b0d5f3b429a400e5d824917f2e86` | `inspect_android_method` | Service attaches controller/NSD to Server and starts listener on configured port (default 4747) |
| `ev_2b7cbc18b4b54e9f4aa80a85c628700489822c5484a90407d3234b21eaa9a19c` | `inspect_android_method` | Server TCP accept loop, timeout, keep-alive and dispatch |
| `ev_4e70b87346e4367dc30c3d37efb71b323492a80d0ad3f47bb3d1459c2f809fe8` | `inspect_android_method` | `/v4` and `/v5` video route dispatch and legacy route handling |
| `ev_391ed7e8d1166fa8e41b31b500b133736f8bd2dc7834a0b90a4a7993b3a4f9b2` | `inspect_android_method` | HTTP-like device/control endpoint dispatcher |
| `ev_d4f4f5054a5cc2201264173bdf2663660a01ac4ff6b1c78c3a7e111a7439afee` | `inspect_android_method` | Resolution parsing, MJPEG height limit and requested send-buffer size |
| `ev_40912752e1d5c5339ea525a61376d34aff49faec48a0dce6dbc20d22ba2edeb4` | `inspect_android_method` | Codec token mapping (`avc`, `jpg`, `hevc`) |
| `ev_cba6d7307479eeb756f6733b597aac72912a78fbddf4cfa56f8363d13f35cfad` | `inspect_android_class` | VideoEncoder inventory and native declarations |
| `ev_667ef84ac0aa2b8d43a457e9316fb49fd411c438b96ffe956213b38e4159f639` | `inspect_android_method` | Negotiated video config selects adapter, logs parameters, calls native init |
| `ev_3b4987949da1a7ee27c5e4d3566b09ebc614e3ee95caa47609752f83bf30abff` | `inspect_android_method` | `VideoEncoder.EncoderInit` native declaration |
| `ev_4753b5e1f5d85777ce49397361d6a5d422db48cb05210d532f81030502a7cad6` | `inspect_android_method` | `VideoEncoder.EncoderRun` native declaration |
| `ev_354e030b2fd6396b648b90520ff8b55f3c969098a760c6c02be97dc78ccced2c` | `inspect_android_method` | `VideoEncoder.EncoderRunBB` native declaration |
| `ev_013ce4bef5129e14c99389b8c00dee234adf47cb2cdb1e5ae00e0a645060aaa2` | `inspect_android_method` | Video encoder release calls native `EncoderRelease()` |
| `ev_1a9b77f6ee1e101c88fa0da96508cd7f5822af4316055e5d4f4a3273c9aa1033` | `inspect_android_method` | SurfaceView holder callback attached by VideoEncoder |

The identifiers above refer to the exact local `base.apk` SHA in the inventory table. REA MCP was unavailable in this already-running Codex session; these records were generated through the configured REA CLI/JADX fallback.

## Clone implementation prerequisites

- Android Studio is installed and `adb` is available; Java 17 is installed. A system `gradle` command is not currently available, so the Android project should use the Gradle wrapper.
- CMake is installed. CMake generator `Ninja`, MSVC `cl.exe`, `clang-cl`, and `msbuild` are not on PATH; the inspected Visual Studio 2022 directory was empty while `vswhere.exe` was present. The Windows native build therefore needs an available MSVC/Windows SDK toolchain before it can be compiled. No toolchain was installed during research.
- Installed Android SDK currently includes platform 35 and Build Tools 34.0.0/35.0.1; API 36 platform/build tools are not present. The clone's proposed compile/target API 36 therefore requires installing those SDK packages before the Android build.
