# Release CI/CD implementation plan

**Goal:** Provide a reproducible Android + Windows build pipeline and publish signed release artifacts from SemVer tags.

**Architecture:** Separate GitHub Actions workflows build PR/main code without signing secrets and build version tags with Android signing, Windows Release packaging, then a restricted final job uploads both artifacts to GitHub Releases. The workflows are documented in `docs/ci-cd.md`.

**Spec:** `docs/superpowers/specs/2026-10-24-release-ci-design.md`.

## Task 1: Build, package and publish tagged releases

Files:

- `.github/workflows/ci.yml` — PR/main builds and temporary artifacts, with no signing secrets.
- `.github/workflows/release.yml` — tag-only signed builds, artifact upload and release publishing.
- `android/app/build.gradle.kts` — release signing from environment and version values from Gradle properties.
- `README.md` — build badge/instructions and release download pointer.
- `docs/ci-cd.md` — signing secret setup, tag/version rules, artifacts, permissions and recovery steps.

Steps:

1. [x] Configure Android release signing from environment variables and allow workflow-supplied version code/name.
2. [x] Add separate CI and tag-release workflows, pinning third-party actions to verified upstream commit SHAs; build Android and Windows in isolated jobs.
3. [x] On tags only, derive an Android version code from `vMAJOR.MINOR.PATCH`, require all signing secrets, create checksummed deliverables, and publish with the minimum write permission.
4. [x] Document how to provision/back up the Android upload key and trigger a release. Do not create a key, set GitHub secrets, push code, or push a tag.
5. [x] Validate workflow YAML with actionlint v1.7.12, assemble Android debug and release variants, build Windows Release, and inspect the final diff. Automated tests are outside scope.

Expected: CI compiles both applications and PR/main runs retain artifacts without referencing signing secrets. A correctly configured version tag creates a GitHub Release with signed APK, Windows x64 ZIP and checksums. The tag workflow must fail before release publication if any signing secret is missing or the tag is not canonical SemVer.

**Validation outcome:** actionlint passes both workflows; Android debug+release assembly succeeds locally (the local release APK is unsigned because no release key was supplied); Windows x64 Release build succeeds. Tag-triggered GitHub jobs and actual Release publication have not run.
