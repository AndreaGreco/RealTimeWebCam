# Changelog

All notable changes to RT-VirtualCam will be documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/).

---

## [1.0.0] - 2026-07-17

First stable release. The receive pipeline is now a single, tunable FFmpeg user-space engine feeding the Media Foundation virtual camera through shared memory.

### Added
- **FFmpeg receive path (now the only one).** The app decodes the RTSP stream in user space with FFmpeg (`RTCamNative/FfmpegRtspSource` — d3d11va hardware decode when available, else software, with low-latency demux options and a resync-to-live latency cap) and streams NV12 frames to the Frame Server through a frame shared-memory channel (`Shared/VCamFrameChannel.h`, triple-buffered, seqlock, created by the Frame Server via `FrameChannelReader` and written by the app via `FrameChannelWriter`). The Frame Server reads those frames in `MediaStream::RequestSample` and shows the synthetic frame when the app producer's heartbeat goes stale. FFmpeg (LGPL, dynamic, decode-only) is provided by vcpkg manifest mode with vcpkg as a git submodule at `external/vcpkg` (version pinned via the baseline); its DLLs are app-locally deployed into the build output and bundled by the MSI — no manual setup for the end user.
- The **live preview** is now FFmpeg-based too (`RTCamNative/FfmpegPreviewPlayer`): the same decode core renders NV12→BGRA into the WinForms panel with a double-buffered GDI path. It inherits the latency cap, so a fast/misbehaving source no longer drifts the way the old Media Foundation preview did.
- Live Frame-Server stats channel (cross-process shared memory, `Shared/VCamStats.h`): RX/render/declined fps and copy cost, surfaced in the app's diagnostics bar while the virtual camera is running. The GPU-decode indicator comes from the app producer (`VCam_IsFfmpegProducerHardware`), shown as `FFmpeg HW`/`FFmpeg SW`.
- **Selectable RTSP transport, with a live view of the one actually in use.** Settings → Network offers *Auto (UDP, TCP fallback)* / *UDP only* / *TCP only*. In Auto the decode loop drives the fallback itself — it opens with an explicit UDP transport first and, if an attempt connects but carries no frame within the socket timeout, flips to TCP on the next attempt — so the connection panel shows the **real** transport (`UDP`/`TCP`) rather than just the request. Exposed to the UI via `VCam_GetActiveTransport` (producer) and `GetActiveTransport` (preview).
- **FFmpeg fine-tuning in Settings** (process-wide, applied on the next connection): hardware-decode on/off (force software decode when a d3d11va driver misbehaves), socket timeout, RTP reorder-buffer depth, and the resync-to-live latency-cap threshold. Persisted in `Settings` and pushed to the native engine through `VirtualCameraWrapper.ApplyEngineSettings`.
- **Diagnostic frame-counter overlay** (Settings, off by default): burns a per-frame counter into every delivered frame (real and synthetic) so the actual consumer-side frame rate is visible on the video itself.
- Settings dialog strings localized in all four supported languages (Italian, English, Spanish, German).

### Removed
- **The Media Foundation receive engine, entirely.** The Frame Server no longer opens the RTSP source itself: `RtspSessionManager` and `RtspReaderCallback` (in-process `IMFSourceReader` + DXVA GPU zero-copy) were removed, as was the Media Foundation preview player (`VideoPlayer`/`VideoReaderCbk`/`CSourceOpenMonitor`). The engine selector (Settings → "Motore di ricezione", `Settings.VideoEngine`, `MF_VCAM_ENGINE` attribute, `VCamConfig.engine`) is gone — there is a single FFmpeg receive path. The virtual camera itself stays Media Foundation; only the source of its pixels changed. As a consequence the camera is live only while the app is running (the decode happens in the app process); this was previously the FFmpeg-mode trade-off and is now the only behaviour.

### Changed
- `deploy_vcam.ps1` now copies the built DLL to `C:\Projects\RTVirtualCamera` (override with `-DeployDir`) before registering it, instead of registering directly from the repo build output under `C:\Users\...` — fixes `E_ACCESSDENIED` on `IMFVirtualCamera::Start` (Frame Server runs as Local Service, which cannot access the user profile).
- `VCamMediaSource::SetupCameraSettings` clamps an implausible RTSP-declared frame rate (>60fps) to 30fps, instead of trusting it for `MediaStream` pacing.
- `MediaStream::RequestSample` tracks how many deliveries re-serve the same frame as the previous one (`declinedFrames`, count only) instead of leaving `renderedFrames` indistinguishable from genuinely new frames. (An attempt at actually declining these re-serves via `MF_E_SAMPLEALLOCATOR_EMPTY` was reverted — it destabilized the Frame Server's own retry timing on real hardware.)
- **Default RTSP transport is now UDP with automatic TCP fallback** (previously hardcoded TCP); a small RTP reorder buffer keeps UDP usable without shredding the picture on minor packet reordering.
- Frame Server sample delivery reworked to an **async two-queue** design, decoupling the RTSP receive cadence from the consumer's `RequestSample` polling.
- **Trace output is off by default in production.** ETW (`WINTRACE`) and the app file log (`DebugLog`) are gated behind runtime environment variables (`RTVCAM_TRACE` machine-wide for the Frame Server, `RTVCAM_LOG` for the app), read once and cached; when disabled they pay no formatting or file-I/O cost and `WINTRACE` skips `EventRegister` entirely.
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

[1.0.0]: https://github.com/andrea-greco/RealTimeWebCam/compare/0.2.0...1.0.0
[0.2.0]: https://github.com/andrea-greco/RealTimeWebCam/compare/0.1.0...0.2.0
[0.1.0]: https://github.com/andrea-greco/RealTimeWebCam/releases/tag/0.1.0
