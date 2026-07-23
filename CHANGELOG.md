# Changelog

All notable changes to RT-VirtualCam will be documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/).

---

## [1.1.0] - 2026-07-23

Reliability, tunability and preview-performance release. Makes a high-bitrate RTSP source (e.g. a 1080p60 stream) hold its frame rate without the packet-loss corruption ("green bands") that aggressive low-latency UDP defaults caused, and surfaces what the engine is actually doing.

### Added
- **Two more FFmpeg engine options in Settings → Network**, both applied on the next connection:
  - **UDP socket buffer** (`buffer_size`) — enlarges the kernel receive buffer so the packet bursts of a high-bitrate 1080p frame don't overflow it (the silent-drop cause of green bands). Shown in KB; 0 leaves libav's default.
  - **Max delay** (`max_delay`) — demuxer reorder delay, previously hard-coded to 0, now tunable (ms).
- **Inline "?" help on every engine option**: hover shows a one-line explanation, click opens the guide at that option's section. Backed by a single shared table (`EngineOptionHelp`) so the tooltips and the guide never drift.
- **In-app engine guide** (`EngineGuideForm`, menu *Guide*): one scrollable section per option with a fuller explanation and a link to the relevant FFmpeg documentation. Localized in all four languages (IT/EN/ES/DE).
- **Live measured received bitrate.** The SDP rarely advertises a bitrate on live RTSP (so the probe showed *n/a*); it is now computed from the actual demuxed packet sizes over a ~1s window and shown live in the Connection panel — `VCam_GetFfmpegProducerBitrate` (producer) / `GetPreviewBitrate` (preview).
- **Active engine settings shown in the Connection table**: transport preference, hardware decode, socket timeout, RTP reorder, UDP buffer, max delay and latency cap, mirrored live from the current settings.

### Changed
- **New "real-time but robust on average" defaults.** `reorder_queue_size` 8 → **512** and UDP `buffer_size` 0 → **2 MB**, so UDP survives a high-bitrate stream (no more green bands) while `max_delay=0` + `nobuffer` + the latency cap keep it real-time. Existing `settings.json` values are preserved — the new defaults only apply to fresh installs.
- **Preview rendering decoupled from decoding.** The decode thread's sink now only packs the latest NV12 frame into a staging buffer and signals; a dedicated render thread does the NV12→BGRA convert and the GDI blit. Previously that ~15–18 ms convert+blit ran on the decode thread and throttled the receive rate below the source's fps, building a backlog that the latency cap then dropped as progressive frame loss. The GDI stretch also moved from `HALFTONE` to the far cheaper `COLORONCOLOR`.
- **Diagnostics tables no longer flicker.** Each refresh is batched with `BeginUpdate`/`EndUpdate` (one repaint per tick instead of one per cell), cells are only rewritten when their value actually changed, and both `ListView`s are double-buffered.

### Fixed
- Bitrate row showed *n/a* on live RTSP sources; it now reflects the measured throughput.

## [1.0.1] - 2026-07-18

Maintenance release: a small UI-string fix and documentation brought in line with the FFmpeg receive engine.

### Fixed
- Italian "Start" button label showed `Avvia!!` (stray exclamation marks) instead of `Avvia`.

### Documentation
- **README (EN + IT) corrected for the FFmpeg receive path.** The receiver is no longer Media Foundation, so the guidance built around MF's packetization quirks is gone:
  - Removed the "force a single H.264 slice per frame / `sliced-threads=0` is non-negotiable" warning — FFmpeg reassembles multi-slice streams correctly (as `ffplay` always did). The encoder recipe stays, reframed as general low-latency advice.
  - Dropped the "RX ≫ real framerate ⇒ multi-slice H.264" diagnostic tell and the matching troubleshooting entry, both of which described the removed MF symptom.
  - Rewrote the diagnostics section for the current UI: two panels (**Connection** / **Stats**) instead of one bar, the full current field set (State, Engine, Decode, RX, Render, Duplicates, Dropped, Processing, Drift), and the fact that the stats reflect the Frame Server once the virtual camera is running (not preview-only).
  - Documented the settings added in 1.0.0 (selectable RTSP transport with live active-transport display, FFmpeg engine tuning, frame-counter overlay).

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

[1.1.0]: https://github.com/andrea-greco/RealTimeWebCam/compare/1.0.1...1.1.0
[1.0.1]: https://github.com/andrea-greco/RealTimeWebCam/compare/1.0.0...1.0.1
[1.0.0]: https://github.com/andrea-greco/RealTimeWebCam/compare/0.2.0...1.0.0
[0.2.0]: https://github.com/andrea-greco/RealTimeWebCam/compare/0.1.0...0.2.0
[0.1.0]: https://github.com/andrea-greco/RealTimeWebCam/releases/tag/0.1.0
