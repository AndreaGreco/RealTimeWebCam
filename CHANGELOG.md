# Changelog

All notable changes to RT-VirtualCam will be documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added
- Live Frame-Server stats channel (cross-process shared memory, `Shared/VCamStats.h`): RX/render/declined/dropped fps, drift, copy cost, and GPU hardware-acceleration capable/active, surfaced in the app's diagnostics bar while the virtual camera is running.

### Changed
- `deploy_vcam.ps1` now copies the built DLL to `C:\Projects\RTVirtualCamera` (override with `-DeployDir`) before registering it, instead of registering directly from the repo build output under `C:\Users\...` — fixes `E_ACCESSDENIED` on `IMFVirtualCamera::Start` (Frame Server runs as Local Service, which cannot access the user profile).
- `VCamMediaSource::SetupCameraSettings` clamps an implausible RTSP-declared frame rate (>60fps) to 30fps, instead of trusting it for `MediaStream` pacing.
- `MediaStream::RequestSample` now tracks how many deliveries re-serve the same RTSP frame as the previous one (`declinedFrames`, count only) instead of leaving `renderedFrames` indistinguishable from genuinely new frames. (An attempt at actually declining these re-serves via `MF_E_SAMPLEALLOCATOR_EMPTY` was reverted — it destabilized the Frame Server's own retry timing on real hardware.)
- MSI (`Setup`) no longer builds in Debug configurations (Release only).

---

## [0.2.0]

### Added
- Spanish and German added to the language selector (alongside Italian and English).
- New application icon (webcam glyph on navy rounded-square background), used by the app and by the installer's Add/Remove Programs entry.
- Installer: `MajorUpgrade` support for clean in-place upgrades, and a Start Menu shortcut.

### Changed
- Migrated `RTVirtualCamera` to .NET 10 (LTS), SDK-style project, self-contained deployment — the MSI no longer requires a pre-installed .NET runtime on the target machine.
- Minimum supported Windows version set to Build 22000 (Windows 11 21H2), matching the `MFCreateVirtualCamera` API requirement.
- About dialog redesigned.
- Settings dialog redesigned; language selector changed from radio buttons to a dropdown.

### Removed
- `PerformanceTests1` project (placeholder MF-infrastructure tests, never wired into the solution).

---

## [0.1.0] - 2026-06-14

### Added
- GPU zero-copy frame path: when the H.264/H.265 decoder runs on the GPU (DXVA),
  frames are copied with a single `CopySubresourceRegion` — no CPU copy, no allocation.
- RTSP auto-reconnection: 60 attempts at 1-second intervals after a stream drop.
- WiX installer (`RT-VirtualCam-Setup.msi`) with version derived from git tags.
- Settings form with localization support (IT/EN).
- Auto-start option that opens the stream on application launch without blocking the UI.
- About page.
- Performance test project for `MediaStream::CopyRtspFrame`.
- CI workflow (GitHub Actions) with MSI artifact upload.

### Changed
- Renamed the Media Foundation bridge project from `MFPipeline` to `RTCamNative`.
- Preview stats bar moved from bottom to top of the window.
- User data (settings, logs) moved to `%LOCALAPPDATA%`.
- RTSP preview latency reduced.

### Fixed
- Bugs A/B/C/D/F/G/H identified in code review (see `CLAUDE.md` for details).
- `LNK2001 QISearch` in `RTCamNative` Release|x64.
- `PAUSED→RUNNING` transition (`SetStreamState`) returning `E_POINTER`.
- Memory leak on Stop/Start cycle.

[Unreleased]: https://github.com/andrea-greco/RealTimeWebCam/compare/0.1.0...HEAD
[0.1.0]: https://github.com/andrea-greco/RealTimeWebCam/releases/tag/0.1.0
