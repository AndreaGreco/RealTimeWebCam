# Changelog

All notable changes to RT-VirtualCam will be documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

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

[Unreleased]: https://github.com/andrea-greco/RealTimeWebCam/compare/1.0.0...HEAD
[1.0.0]: https://github.com/andrea-greco/RealTimeWebCam/releases/tag/1.0.0
