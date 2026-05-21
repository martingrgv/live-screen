---
mode: agent
description: Build Live Screen, bundle libvlc + runtime DLLs into a zip, and publish a GitHub pre-release with the asset attached via gh CLI.
---

# Ship a Live Screen release

You are shipping a new Windows x64 release of Live Screen. The deliverable is a single `.zip` that a user can extract anywhere and run `LiveScreen.exe` without installing anything except (optionally) the VC++ redistributable. Publish it as a **pre-release** on GitHub via `gh`.

Work autonomously — don't ask for confirmation between steps. If something fails, fix it and continue.

## Inputs

Before doing anything, decide the version tag:

- List existing tags with `git --no-pager tag --sort=-v:refname` and pick the next pre-release tag. Format: `vMAJOR.MINOR.PATCH-pre.N` (e.g. previous `v0.2.0-pre.1` → next `v0.2.0-pre.2`, or `v0.3.0-pre.1` if the change set warrants a minor bump). If the user supplied a tag in their message, use that instead.
- The "change set since last release" is `git --no-pager log <last-tag>..HEAD --oneline`. Read these commits to write the release notes; do not invent features.

## Required steps

1. **Sync `main`.**
   - `git status` must be clean. If there are uncommitted changes, stop and report — do not commit on the user's behalf for a release.
   - `git push origin main` so the tag points at a commit GitHub can see.

2. **Build Release x64.**
   - Locate MSBuild: it lives at `C:\Program Files\Microsoft Visual Studio\<version>\Community\MSBuild\Current\Bin\MSBuild.exe` (use `vswhere` if the exact path is unknown).
   - Run: `MSBuild.exe cpp\LiveScreen.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal`
   - The output is `cpp\x64\Release\LiveScreen.exe`. Fail the task if this file is missing after the build.

3. **Stage the bundle** at `release-stage\LiveScreen-<tag>-win-x64\` (delete and recreate `release-stage\` each time so old artifacts don't leak in):
   - `LiveScreen.exe` from `cpp\x64\Release\`.
   - `libvlc.dll` and `libvlccore.dll` from `C:\Program Files\VideoLAN\VLC\`.
   - The entire `plugins\` folder from `C:\Program Files\VideoLAN\VLC\plugins`. **Do not** prune it — libvlc loads modules dynamically and missing plugins manifest as silent decode/aout failures at runtime.
   - `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` from `C:\Windows\System32\` (the MSVC v145 toolset links the dynamic CRT).
   - A `README.txt` covering: how to run, where settings are stored (`%APPDATA%\LiveScreen\config.ini`), tray menu items, the LGPL note for libvlc with a link to https://www.videolan.org/legal.html, and a fallback line pointing users to the "Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64)" if `vcruntime` errors appear.

4. **Zip it** to `release-stage\LiveScreen-<tag>-win-x64.zip` (`Compress-Archive -Path "...\*" -DestinationPath ... -Force`). Verify the file exists and is non-trivial (>10 MB — the plugins folder alone is ~50 MB).

5. **Tag and push.**
   - `git tag -a <tag> -m "<tag>"`
   - `git push origin <tag>`

6. **Create the pre-release** with `gh`:
   - `gh` lives at `C:\Program Files\GitHub CLI\gh.exe` if it isn't on PATH. Confirm auth with `gh auth status`.
   - Write release notes to a temp file (PowerShell here-string → `Out-File -Encoding utf8`), then:
     ```
     gh release create <tag> --prerelease --title "<tag>" --notes-file <tmp>
     gh release upload <tag> release-stage\LiveScreen-<tag>-win-x64.zip --clobber
     ```
   - Delete the temp notes file when done.

7. **Cleanup.** Remove the `release-stage\` directory after a successful upload so it doesn't get committed by accident. (`release-stage/` should also be in `.gitignore`; add it if it isn't.)

## Release notes format

Use this skeleton, populated from the `git log` range:

```
Pre-release for testing.

## Highlights since <previous-tag>

- **<Feature>.** <One-sentence user-visible impact.>
- **<Fix>.** <What changed and why a user cares.>
- ...

## Install

1. Download `LiveScreen-<tag>-win-x64.zip` below.
2. Extract anywhere and run `LiveScreen.exe`.
3. On first launch, pick a video. Right-click the tray icon for Mute / Change Wallpaper / Exit.

If `LiveScreen.exe` fails with a `VCRUNTIME140.dll` error, install the
Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64).
```

Group bullets by user-visible theme (playback, UI, persistence, performance). Skip refactors and build-system commits unless they affect the shipped binary. Do not include internal commit hashes.

## Hard rules

- Pre-release only — always pass `--prerelease`. Never promote to a full release without an explicit user request.
- Never bundle a sample video (licensing).
- Never check `release-stage\` or the zip into git.
- Never amend or rewrite the tag once it is pushed; if you need to retry, bump to the next pre-release number.
- The bundle target is **x64 only**. The Win32 configs in `LiveScreen.vcxproj` don't link libvlc and must not be shipped.

## Verify before finishing

- `gh release view <tag>` shows the release marked as Pre-release, with the zip listed under Assets and size matching what you uploaded.
- Report the release URL and the asset name to the user as the final summary.
