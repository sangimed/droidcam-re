# Release CI/CD design

## Intent

Build Android and Windows deliverables for the DroidCam clone and publish a GitHub Release when a maintainer pushes a version tag. Pull requests and pushes to `main` compile both applications and retain installable/buildable CI artifacts without publishing a release.

## Goals

- Compile Android with JDK 17, Android SDK 36 and the checked-in Gradle wrapper.
- Compile the Windows x64 CMake target on a GitHub-hosted Windows runner.
- Produce a consistently signed Android release APK and a Windows x64 ZIP with SHA-256.
- Publish both assets and generated release notes for canonical `vMAJOR.MINOR.PATCH` tags.
- Keep workflow permissions read-only except the final release-publishing job.

## Non-goals

- Publishing a release during CI setup.
- Committing a signing key or generating a nonrecoverable release identity in CI.
- Signing the Windows executable or installer creation.
- Running automated tests; only build validation is in scope for this change.

## Approaches

1. **Separate CI and release workflows (selected).** PR/main builds never reference signing secrets; tag builds have the release key and publishing permission. This duplicates some build steps but keeps secret-bearing jobs outside PR-triggered workflow execution.
2. **One combined workflow.** This reduces duplication but places PR and secret-bearing release logic in one workflow file. A PR from a repository collaborator could change that file and attempt to expose secrets, so this is not selected.

The release tag must match `vMAJOR.MINOR.PATCH`. Android `versionCode` is derived monotonically from those numeric components. An Android upload keystore and its passwords are supplied as GitHub Actions secrets; a tag build fails before publishing if they are absent. The release workflow never falls back to a debug signature for a public release.

## Release assets

- `droidcam-clone-android-vX.Y.Z.apk`: signed release APK.
- `droidcam-clone-windows-vX.Y.Z-x64.zip`: Windows x64 executable and usage README.
- `SHA256SUMS.txt`: SHA-256 of the release archives/APK.

Both workflows default to read-only repository permission. Only the final job in the tag-only release workflow receives `contents: write` to create the GitHub Release.

## Operator setup

Before the first release, generate and back up a private Android upload keystore, then add these repository Actions secrets: `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, and `ANDROID_KEY_PASSWORD`. Do not commit the keystore or its passwords. Push a `vX.Y.Z` tag at a commit containing the workflow to create the release.

## Validation

- Run Android debug and release Gradle assembly locally when a signing key is available; otherwise compile debug locally and validate the release configuration structurally.
- Run the Windows x64 Release CMake build.
- Validate YAML syntax and inspect that no write token is exposed to build steps.
- Do not create/push a tag or publish a release as part of setup.
