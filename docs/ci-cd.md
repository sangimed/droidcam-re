# CI/CD and release guide

The workflow at `.github/workflows/ci.yml` compiles both platforms on pull requests to `main` and pushes to `main`. Successful builds retain the Android APK and Windows x64 ZIP as downloadable Actions artifacts for 14 days. It has no signing secrets. No automated test suite is configured yet.

The separate `.github/workflows/release.yml` runs only for pushed `v*` tags. It validates canonical SemVer, signs the Android APK, builds the Windows x64 ZIP, and publishes the release. Only its publishing job receives `contents: write` permission.

Pushing a tag in canonical `vMAJOR.MINOR.PATCH` form builds a signed Android release APK and a Windows x64 ZIP, computes SHA-256 checksums, and publishes a GitHub Release with generated notes. Examples: `v0.1.0`, `v1.2.3`. Noncanonical tags fail validation; tags do not silently publish a debug-signed APK.

## One-time Android signing setup

Android updates must keep the same signing key. Back up the upload keystore and its passwords in a secure password manager and offline storage. If the key is lost, users cannot install an update over the existing signed app.

Generate a private upload key on a trusted workstation; do not put the `.jks` file in this repository:

```powershell
keytool -genkeypair -v -keystore droidcam-clone-upload.jks -keyalg RSA -keysize 4096 -validity 10000 -alias droidcam-clone
$keystoreBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Resolve-Path .\droidcam-clone-upload.jks)))
Set-Clipboard $keystoreBase64
```

In GitHub, open **Settings → Secrets and variables → Actions → New repository secret** and add:

| Secret | Value |
|---|---|
| `ANDROID_KEYSTORE_BASE64` | Clipboard contents from the command above |
| `ANDROID_KEYSTORE_PASSWORD` | Keystore password chosen with `keytool` |
| `ANDROID_KEY_ALIAS` | `droidcam-clone`, unless another alias was chosen |
| `ANDROID_KEY_PASSWORD` | Password for that key |

The release job checks that all four secrets exist before it restores the key. The keystore is written to the ephemeral runner temp directory and is not uploaded as an artifact. The release job is the only job with `contents: write`; build jobs use read-only repository permissions.

## Create a release

Make sure the commit is on `main` and the signing secrets are configured. Then create and push a new tag:

```powershell
git switch main
git pull --ff-only
git tag v0.1.0
git push origin v0.1.0
```

Use a new, higher SemVer version for each release. The Android `versionCode` is derived as `MAJOR × 1,000,000 + MINOR × 1,000 + PATCH + 1`, with each component bounded to preserve Android's supported range and keep `v0.0.0` valid. The tag version becomes the Android `versionName` and release title.

When the Actions run completes, the GitHub Release contains:

- `droidcam-clone-android-vX.Y.Z.apk` — signed APK for Android sideloading.
- `droidcam-clone-windows-vX.Y.Z-x64.zip` — Windows x64 client and README.
- `SHA256SUMS.txt` — checksums for both release files.

The Windows ZIP contains the executable, not an installer. Windows system libraries are supplied by the operating system. Users may need to allow the client through the private-network firewall (TCP 48200 and the negotiated UDP video port).

## CI artifacts

Pull request/main builds use the debug APK and a Windows Release-mode executable archive. These are short-lived CI artifacts for review and manual installation; only version-tag runs publish release assets. The Android release build can also be assembled locally with Gradle properties `APP_VERSION_CODE` and `APP_VERSION_NAME`, plus `ANDROID_KEYSTORE_PATH`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, and `ANDROID_KEY_PASSWORD` environment variables.
