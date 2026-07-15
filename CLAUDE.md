# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Open `RTVirtualCamera.sln` in **Visual Studio 2022** and build in **x64**. All three output binaries land in the same folder (`bin\x64\Debug\` or `bin\x64\Release\`):

| Binary | Project |
|---|---|
| `VCamSampleSource.dll` | `VirtualCamera/` |
| `RTCamNative.dll` | `RTCamNative/` |
| `RTVirtualCamera.exe` | `RTVirtualCamera/` |

There is no CLI build command; this is a Visual Studio solution only. Target: `.NET 10` (C#, self-contained, `net10.0-windows10.0.22000.0`), C++17 (C++), all x64.

### Registering the COM DLL

After every rebuild of `VCamSampleSource.dll`, run the deploy script (self-elevates to Administrator):

```powershell
.\VirtualCamera\deploy_vcam.ps1 -DllPath ".\bin\x64\Debug\VCamSampleSource.dll"
```

The DLL must be registered from a folder accessible to all users — **not under `C:\Users\...`** — because the Frame Server runs as *Local Service*; a path under your user profile causes `E_ACCESSDENIED` on `IMFVirtualCamera::Start`. `deploy_vcam.ps1` handles this itself: it copies `$DllPath` (the normal repo build output, under `C:\Users\...`) to `$DeployDir` (default `C:\Projects\RTVirtualCamera`, override with `-DeployDir`) and registers that copy instead. `unregister_vcam.ps1` defaults to the same deployed path.

To automate after build: add to `VirtualCamera` project → **Post-Build Event**:
```
powershell -NoProfile -ExecutionPolicy Bypass -File "$(ProjectDir)deploy_vcam.ps1" -DllPath "$(TargetPath)"
```

### Debugging the Frame Server

The Frame Server (`svchost.exe`) is a separate OS process. Attach to it in Visual Studio: **Attach to Process** → find `svchost.exe` hosting the *Windows Camera Frame Server* service. Use `WINTRACE` / `DebugLog` macros for trace output — do not rely on the Visual Studio Output window for Frame Server behavior.

---

## Architecture

This project makes an RTSP network camera appear as a standard Windows webcam for Zoom, Teams, etc.

```
RTVirtualCamera.exe (C# WinForms, .NET 4.8)
  └─ P/Invoke → RTCamNative.dll (C++ bridge)
       └─ MFCreateVirtualCamera(CLSID_VCam)
       └─ IMFVirtualCamera::SetString/SetUINT32(...)   ← config attrs, BEFORE Start()
       └─ IMFVirtualCamera::Start()

Windows Frame Server (svchost.exe)  ← separate OS process, Local Service
  └─ loads VCamSampleSource.dll (COM DLL, HKLM registered)
       └─ Activator::ActivateObject()
            └─ VCamMediaSource::SetupCameraSettings(attrs) ← reads RTSP URL + geometry
            └─ VCamMediaSource::Initialize(attrs)          ← creates streams/descriptors
       └─ VCamMediaSource::Start() → RtspSessionManager::Start(config)
            └─ MFCreateSourceReaderFromURL(rtspUrl, ...)   ← opens RTSP in FS process
       └─ MediaStream::RequestSample() ~30x/sec
            └─ RtspSessionManager::TryGetLatestFrame()
            └─ MediaStream::CopyRtspFrame() → sample to Zoom/Teams
            └─ MediaStream::PublishStats() → StatsPublisher (Global\ shared memory)

RTVirtualCamera.exe (UI timer, ~2x/sec)
  └─ RTCamNative::StatsReader::TryGetStats() ← reads the same shared memory
```

**No shared memory for the RTSP frame data itself — in the default Media Foundation engine.** The Frame Server opens the RTSP stream and decodes/copies every frame entirely inside its own process; the C# app never touches pixel data — it only sets config attributes on `IMFVirtualCamera` before calling `Start()`. There **is** a small shared-memory channel, but it carries live *stats* only (fps/drift/copy-cost/HW-accel), one-way from the Frame Server to the app — see "Live stats (Frame Server → app)" below.

**The optional FFmpeg engine reverses this** — there the app decodes the RTSP stream in user space and streams NV12 pixels to the Frame Server through a second, larger shared-memory channel. See "Two receive engines (MF vs FFmpeg)" below.

### Virtual camera lifecycle (detailed)

1. App calls `MFCreateVirtualCamera()` — registers the camera in the system, DLL not yet loaded.
2. App calls `IMFVirtualCamera::SetString(MF_VCAM_RTSP_URL, url)` and `SetUINT32(...)` for width/height/fps — stored in the MF attributes bag.
3. App calls `IMFVirtualCamera::Start()` — Frame Server loads the DLL, calls `Activator::ActivateObject()`.
4. When a consumer (Zoom) opens the camera, Frame Server calls `VCamMediaSource::Start()` → `RtspSessionManager::Start()` which opens the RTSP URL with `IMFSourceReader`.
5. `RequestSample()` is called ~30x/sec; it reads the latest decoded frame from `RtspSessionManager` and copies it into the allocated sample.
6. On stop: `VCamMediaSource::Stop()` → `RtspSessionManager::Stop()` → `Shutdown()`.

---

## Key classes

### `VirtualCamera/` (COM DLL — runs in Frame Server process)

| Class | File | Role |
|---|---|---|
| `Activator` | `Activator.cpp` | `IMFActivate` — entry point when Frame Server loads the DLL. Calls `SetupCameraSettings()` then `Initialize()`. |
| `VCamMediaSource` | `VCamMediaSource.cpp/.h` | `IMFMediaSourceEx` — the camera source. Owns stream lifecycle and delegates frame delivery to `RtspSessionManager`. |
| `MediaStream` | `MediaStream.cpp/.h` | `IMFMediaStream2` — one video stream. On `RequestSample()` pulls from `RtspSessionManager`, copies with `CopyRtspFrame()`, and always delivers something once the first frame has arrived — even a re-serve of the same RTSP frame as last time (tracked as `declinedFrames`, count-only; an earlier attempt at literally declining these via `MF_E_SAMPLEALLOCATOR_EMPTY` destabilized the Frame Server's retry timing and was reverted). Falls back to `FrameGenerator` only before the first frame ever arrives, or in `Reconnecting`/`Failed`/`Starting`, so a disconnected camera still shows its placeholder immediately. |
| `RtspSessionManager` | `RtspSessionManager.cpp/.h` | Singleton (`Instance()`). Opens RTSP with `IMFSourceReader`, keeps the latest decoded frame, decouples RTSP fps from consumer fps. |
| `RtspReaderCallback` | `RtspReaderCallback.cpp` | `IMFSourceReaderCallback` — async read chain. Stores only the latest frame (`_latestSample`); older frames are overwritten. Also keeps the `rxFrames`/`droppedFrames` counters read by `StatsPublisher`. |
| `FrameGenerator` | `FrameGenerator.cpp` | **Dead code** (leftover from VCamSample). Produces synthetic frames via Direct2D; fallback path only, never called in the main stream. |
| `StatsPublisher` | `StatsPublisher.cpp/.h` | Singleton (`Instance()`). Writes `VCamFrameServerStats` into the cross-session shared memory section on every `MediaStream::RequestSample()` tick. See "Live stats" below. |

### `RTCamNative/` (C++ bridge DLL — runs in app process)

| Class | File | Role |
|---|---|---|
| `VirtualCamera` | `VirtualCamera.cpp/.h` | Wraps `IMFVirtualCamera`. Calls `SetString`/`SetUINT32` on the attrs bag before `Start()`. Exports C-style functions for P/Invoke. |
| `VideoPlayer` | `VideoPlayer.cpp` | Preview only (EVR sink into WinForms panel). **Not involved in the Frame Server / Zoom path.** Plays from an RTSP/file URL only — the capture-device source path was removed. |
| `StatsReader` | `StatsReader.cpp/.h` | Singleton (`Instance()`). Opens the same shared memory as `StatsPublisher` (read-only) and exports `VCam_GetFrameServerStats()` for P/Invoke. |

### `RTVirtualCamera/` (C# WinForms, namespace `RTVirtualCamera`)

| Class | File | Role |
|---|---|---|
| `VirtualCameraWrapper` | `VirtualCameraWrapper.cs` | P/Invoke façade over `RTCamNative.dll`. Declares the C# mirror of `VCamConfig`, and of `VCamFrameServerStats` as `FrameServerStats`. `TryGetFrameServerRates()` polls `VCam_GetFrameServerStats` and derives fps from counter deltas. |
| `VideoPlayerWrapper` | `VideoPlayerWrapper.cs` | P/Invoke façade for the preview player. |
| `MainForm` | `MainForm.cs` | Main UI. Probes source, builds `VCamConfig`, calls `SetConfig → Register → Start`. `StatsTimer_Tick` shows preview stats normally, or Frame-Server stats (via `VirtualCameraWrapper.TryGetFrameServerRates`) once the virtual camera is running. |
| `Settings` | `Settings.cs` | Persists `RtspURL` and `AutoStart` across sessions. |

### `Shared/VCamConfig.h`

Single source of truth for the GUIDs and the `VCamConfig` struct shared between `RTCamNative/` and `VirtualCamera/`. Included by both C++ projects. **The C# mirror `VCamCameraWrapper.cs::VCamConfig` must match byte-for-byte.**

### `Shared/VCamStats.h`

Single source of truth for the live-stats wire format: the `VCamFrameServerStats` struct, the `VCAM_STATS_MAPPING_NAME` shared-memory name, and the seqlock convention. Included by both `VirtualCamera/StatsPublisher.cpp` (writer) and `RTCamNative/StatsReader.cpp` (reader). **The C# mirror `VirtualCameraWrapper.cs::FrameServerStats` must match byte-for-byte** (`Pack = 1`, same field order) — this is now a three-way mirror (C++ struct, C++ struct, C# struct), one more place than `VCamConfig` to keep in sync when the layout changes.

---

## Two receive engines (MF vs FFmpeg)

The RTSP stream can be received two ways, chosen by the user (Settings → "Motore di
ricezione", persisted as `Settings.VideoEngine`) and passed to the Frame Server as the
`MF_VCAM_ENGINE` attribute (UINT32) alongside the other config attrs, read in
`SetupCameraSettings`. `VCamConfig.engine` / `CameraSessionConfig.engine` carry it internally.

- **`MediaFoundation` (default, engine=0)** — the path everything else in this document
  describes: the Frame Server opens the RTSP source in-process (`RtspSessionManager` +
  `IMFSourceReader`). Self-contained — the camera keeps working after the app closes.
- **`FFmpeg` (engine=1)** — the app decodes in user space and pushes frames to the Frame Server:

```
RTVirtualCamera.exe (app process)
  MainForm: engine=FFmpeg → after Start(), VirtualCameraWrapper.StartFfmpegProducer(url,w,h,…)
    → RTCamNative!VCam_StartFfmpegProducer  (FfmpegExports.cpp)
      → FfmpegRtspSource (thread): libavformat open RTSP → libavcodec decode (SW)
        → libswscale → NV12 → FrameChannelWriter.WriteFrame()
          → seqlock publish into Global\RTVCam_Frames_<CLSID>  (Shared/VCamFrameChannel.h)

Frame Server (svchost.exe)
  VCamMediaSource::SetupCameraSettings: engine=FFmpeg
    → FrameChannelReader::EnsureMapped(w,h)   ← CREATES the mapping (service privilege)
    → RtspSessionManager is NOT started (VCamMediaSource::Start skips it)
  MediaStream::RequestSample (~30x/sec)
    → CopyFrameChannelFrame() → FrameChannelReader::AcquireLatest() → CopyNv12ToSample()
    → synthetic FrameGenerator frame if the producer heartbeat is stale (>2s) or no frame yet
```

Design points:

- **Producer lives in the app process → app-lifetime.** In FFmpeg mode the camera only has
  live frames while `RTVirtualCamera.exe` is running; on app close the Frame Server shows the
  synthetic frame (heartbeat goes stale). This is inherent to decoding in user space and was
  an accepted trade-off. The MF engine has no such dependency.
- **The frame mapping is CREATED by the Frame Server, WRITTEN by the app** — the reverse data
  direction of the stats channel, but the same "service creates, app opens" shape, because
  creating a `Global\` object needs `SeCreateGlobalPrivilege` (the Local Service Frame Server
  has it; a non-elevated user does not). DACL `D:(A;;GA;;;LS)(A;;GRGW;;;IU)` grants Interactive
  Users read+write. Both handled in `FrameChannelReader::EnsureMapped` /
  `BuildFramesSecurityDescriptor`.
- **Fixed max-size mapping.** The section is created for `VCAM_FRAMES_MAX_*` (3840×2160) and
  the real geometry lives in the header, so it survives resolution changes across sessions
  without a resize (the section outlives a session, like the stats one).
- **Triple-buffered, seqlock-published.** Ring of `VCAM_FRAMES_SLOT_COUNT` (3) NV12 slots; the
  single writer fills the next slot then publishes `latestSlot`+`frameSeq` under the header
  seqlock (same idiom as `Shared/VCamStats.h`). The pixels aren't locked — 3 slots give ~2
  frames of headroom before the writer revisits a slot the reader might still be copying.
- **FFmpeg = LGPL, dynamic, via vcpkg.** Provided by vcpkg manifest mode; vcpkg lives as a git
  submodule at `external/vcpkg`, and `vcpkg.json` pins the version via `builtin-baseline`
  (currently ffmpeg 8.1.2, LGPL, features `avcodec`/`avformat`/`swscale`). `RTCamNative.vcxproj`
  imports vcpkg's MSBuild props/targets; vcpkg app-locally copies the DLLs into the shared `bin`
  and the WiX `<Files>` glob harvests them into the MSI — no manual steps for the end user.
  `THIRD_PARTY_NOTICES.md` covers the LGPL notice. The FFmpeg source files
  (`FfmpegRtspSource.cpp`, `FrameChannelWriter.cpp`, `FfmpegExports.cpp`) are compiled **native**
  (`CompileAsManaged=false`, no PCH) because RTCamNative is `/clr` on x64 and the libav C headers
  don't mix with C++/CLI.

## Critical invariants

- **CLSID** `{3CAD447D-F283-4AF4-A3B2-6F5363309F52}` must match in `VirtualCamera/`, `RTCamNative/`, and the registry. Never change it in only one place.
- **Attribute GUIDs** (`MF_VCAM_RTSP_URL`, `MF_VCAM_WIDTH`, `MF_VCAM_HEIGHT`, `MF_VCAM_FPS_NUM`, `MF_VCAM_FPS_DEN`) are defined in `Shared/VCamConfig.h` and included by both C++ projects — single definition, no duplication risk.
- **Config attrs must be set BEFORE `IMFVirtualCamera::Start()`**. The Frame Server reads them in `Activator::ActivateObject()` → `SetupCameraSettings()`. Attributes set after `Start()` are not re-read.
- **`SetBlob` is NOT reliably forwarded** by the Frame Server to `Initialize()`. Only `SetString` and `SetUINT32` are safe for passing config. Hence five individual attribute calls instead of one struct blob.
- **DLL path constraint**: `VCamSampleSource.dll` must be in a publicly accessible folder (not under `C:\Users\...`), because the Frame Server runs as Local Service.
- **`Activator::ActivateObject()` call order**: `SetupCameraSettings()` must be called before `Initialize()` because `Initialize()` calls `ApplyRuntimeContext()` which reads `_camera_config` that was just filled by `SetupCameraSettings()`. This ordering is enforced only by `ActivateObject()` — it is not self-enforced by the classes.
- **`VCamConfig` struct ABI**: When modifying `Shared/VCamConfig.h::VCamConfig`, always update `RTVirtualCamera/VirtualCameraWrapper.cs::VCamConfig` mirror too. (The `engine` field is the newest addition — it also has a matching `MF_VCAM_ENGINE` attribute GUID and a `VCamEngine` / C# `VideoEngine` enum to keep in sync.)
- **`VCamFrameChannel` wire format**: `Shared/VCamFrameChannel.h` is the single source of truth for the FFmpeg frame channel; the writer (`RTCamNative/FrameChannelWriter.cpp`) and the reader (`VirtualCamera/FrameChannelReader.cpp`) both include it. Bump `VCAM_FRAMES_STRUCT_VERSION` if the header layout changes. Unlike the config/stats mirrors there is no C# mirror — the app touches this channel only through native code.
- **The frame mapping is created ONLY by the Frame Server** (`FrameChannelReader`, `SeCreateGlobalPrivilege`); the app opens it for writing. Do not create it app-side.
- **`VCamFrameServerStats` struct ABI**: When modifying `Shared/VCamStats.h::VCamFrameServerStats`, bump `VCAM_STATS_STRUCT_VERSION` and update `RTVirtualCamera/VirtualCameraWrapper.cs::FrameServerStats` (same field order, `Pack = 1`) in the same change.
- **Stats mapping must use the `Global\` namespace with an explicit DACL**. The Frame Server (Local Service, session 0) and the app (interactive user session) are in different Terminal Server sessions — a session-local name would be invisible to the app, and the default DACL for an object created by a service would deny the app's `OpenFileMappingW` even with the right name. Both are handled once, in `StatsPublisher::EnsureMapped()` / `BuildStatsSecurityDescriptor()`; do not create the mapping anywhere else.

---

## Config flow (frame by frame)

```
C# MainForm                RTCamNative::VirtualCamera      Frame Server
──────────                 ──────────────────────────      ────────────
SetConfig(config)    →     stores _config
Register()           →     MFCreateVirtualCamera()
                           SetString(MF_VCAM_RTSP_URL)
                           SetUINT32(MF_VCAM_WIDTH) ...
Start()              →     IMFVirtualCamera::Start()   →   ActivateObject()
                                                       →   SetupCameraSettings()
                                                       →   Initialize() → ApplyRuntimeContext()
[Zoom opens camera]                                    →   VCamMediaSource::Start()
                                                       →   RtspSessionManager::Start(config)
                                                       →   MFCreateSourceReaderFromURL(rtspUrl)
[30x/sec]                                              →   RequestSample()
                                                       →   TryGetLatestFrame() + CopyRtspFrame()
```

---

## Frame copy path (latency-critical)

The decoded RTSP frame reaches the consumer's sample through `MediaStream::CopyRtspFrame`, which has two paths:

- **GPU zero-copy (hot path)** — `CopyRtspFrameGpu`. When the source reader is given the Frame Server's `IMFDXGIDeviceManager` (via `MF_SOURCE_READER_D3D_MANAGER`), the H.264/H.265 decoder runs on the GPU (DXVA) and emits NV12 textures. Both the source and the allocator-provided destination buffers are `IMFDXGIBuffer` on the **same** D3D11 device, so the copy is a single `ID3D11DeviceContext::CopySubresourceRegion` — **no CPU copy, no allocation**. The shared immediate context is serialized with `IMFDXGIDeviceManager::LockDevice/UnlockDevice`; `MF_E_DXGI_NEW_VIDEO_DEVICE` triggers a handle reopen + one retry.
- **CPU fallback** — `CopyRtspFrameCpu`. Used during pipeline warmup or when HW decode is unavailable (source buffer is system memory, not a DXGI texture). A **single** `MFCopyImage` (Y + UV planes) straight from the source MF buffer into the destination — no intermediate `std::vector`. Honors the decoder's real source pitch via `IMF2DBuffer::Lock2D`.

`RtspFrameSnapshot` carries the decoded `IMFSample` by reference (AddRef), not a pixel copy — `RtspSessionManager::TryGetLatestFrame` holds the session lock only long enough to grab that reference. The D3D device manager flows from `VCamMediaSource::SetD3DManager` → `RtspSessionManager::SetD3DManager` (for the reader) and → each `MediaStream::SetD3DManager` (which opens a cached device handle for the copy). **`SetD3DManager` must arrive before `RtspSessionManager::Start()`** for the GPU path to engage; otherwise the reader runs in system memory and the CPU fallback is used. Check the `WINTRACE` in `RtspSessionManager::Start` to confirm which path is active.

## Live stats (Frame Server → app)

`MainForm`'s diagnostics bar shows two different things depending on whether the virtual camera is running: while only previewing, it reads the in-process `VideoPlayerWrapper` preview stats (unchanged, always worked); once the virtual camera is started, the *real* pipeline runs inside the Frame Server process and the app can't read it directly — that gap is what this channel closes.

```
MediaStream::RequestSample() (~30x/sec, Frame Server)
  → assembles a VCamFrameServerStats snapshot:
       rxFrames/droppedFrames   ← RtspSessionManager::GetFrameCounters() → RtspReaderCallback
       renderedFrames/declinedFrames/driftMs/lastCopyMs/hwAccelActive ← tracked in MediaStream itself
       hwAccelCapable           ← MediaStream::_deviceManager && _deviceHandle
  → StatsPublisher::Publish(snapshot)
       seqlock write into Global\RTVCam_Stats_<CLSID> shared memory

MainForm.StatsTimer_Tick (~2x/sec, app UI thread)
  → VirtualCameraWrapper.TryGetFrameServerRates()
       → RTCamNative!VCam_GetFrameServerStats() → StatsReader::TryGetStats()
            seqlock read from the same shared memory
       → derives fps from counter deltas across two polls (same technique as
         VideoPlayerWrapper.TryGetRates for the preview path)
```

Design points worth remembering:

- **Seqlock, not a named `Mutex`.** The writer runs on `MediaStream::RequestSample`, the same latency-critical path as the GPU zero-copy frame copy (see above) — a kernel `Mutex`/`CRITICAL_SECTION` acquire per frame is avoidable overhead there. `VCamFrameServerStats::sequence` is bumped odd→even around the writes (`InterlockedIncrement`, no blocking); the reader retries (bounded, 8 attempts) if it observes an odd sequence or the sequence changed mid-read. There is exactly one writer, which is what makes this safe without a heavier primitive.
- **`hwAccelCapable` vs `hwAccelActive`.** `Capable` means a D3D11 device manager was successfully handed to this `MediaStream` (`SetD3DManager` succeeded) — the *precondition* for the zero-copy path. `Active` means the *last* `CopyRtspFrame` call actually took that path; it can be `Capable=1, Active=0` right after `CopyRtspFrameGpu` fails and falls back to CPU for one frame (see `CopyRtspFrame`'s fallback branch) — that combination is the signal to look for if GPU decode looks unhealthy on a given machine.
- **Staleness.** `VCamFrameServerStats::updatedTickMs` is `GetTickCount64()` at the last publish. `VirtualCameraWrapper.TryGetFrameServerRates` flags `Stale = true` once the app hasn't seen it advance for >3s, so the UI can distinguish "camera stopped" from "app briefly missed a poll" from "Frame Server process died without cleaning up."
- **`renderedFrames` vs `declinedFrames` vs `rxFrames`.** `RtspReaderCallback::TakeLatestSample` does not clear the cached frame after handing it out — the same decoded frame keeps being available until `OnReadSample` stores a newer one, identified by `RtspFrameSnapshot::frameSeq` (bumped only when a genuinely new sample arrives). `MediaStream::RequestSample` compares the frame it just got against the last one it actually delivered (`_lastDeliveredFrameSeq`) and **always copies and delivers it either way** — this is deliberate: an earlier version declined the re-serve instead (`MF_E_SAMPLEALLOCATOR_EMPTY`, no `MEMediaSample` queued, same idiom as the allocator-exhausted case a few lines above), and on real hardware that destabilized the whole pipeline (occasional synthetic frames, general instability) because that HRESULT is tied to the allocator's own release-notification retry mechanism, not a generic "ask again later" signal — overloading it broke the Frame Server's retry timing. So `renderedFrames` counts every delivery (matches what the consumer actually receives, `frameSeq` match or not); `declinedFrames` is a same-`frameSeq` counter kept for diagnostics only, no behavior attached. `renderedFrames` can legitimately run well ahead of `rxFrames` — that's the consumer (Zoom/Teams) polling faster than the camera delivers, not a bug — `declinedFrames` tells you how much of `renderedFrames` that is.
- **The mapping outlives any single camera session.** `StatsPublisher` is a process-wide singleton created once and never explicitly torn down; a finished session just stops advancing `updatedTickMs` (caught by the staleness check above) rather than the mapping disappearing.

## RTSP reconnection

When the stream drops (network blip, camera reboot), `RtspReaderCallback::OnReadSample` sees a failed `hrStatus` or end-of-stream and fires its broken-handler **once** (outside the callback lock, to avoid a lock inversion with the manager). `RtspSessionManager::NotifyStreamBroken` then sets state `Reconnecting` and spawns a single reconnect thread (`ReconnectLoop`) that retries `OpenReaderLocked` **`kMaxReconnectAttempts` (60) times, `kReconnectIntervalMs` (1000 ms) apart** — i.e. 60 attempts at 1/sec. On success → `Running`; after 60 failures → `Failed`. While `Reconnecting`, `TryGetLatestFrame` returns `MF_E_SHUTDOWN` so the consumer sees the `FrameGenerator` "Camera IP non connessa" frame, not a frozen last image. The reconnect thread is signalled+joined (outside `_lock`) by `Start`/`Stop` via `StopReconnectThread`. There is no single MF flag that yields exactly "60×1s" for RTSP (`MFNETSOURCE_AUTORECONNECTLIMIT` exists but timing isn't controllable and RTSP honoring is uncertain), hence the explicit loop.

---

## Known bugs

Bugs A/B/C/D/F/G/H below were fixed in `0.1.0` (see `CHANGELOG.md`); verified against current source on 2026-07-09. Only **E** is still open.

| # | Severity | File / Location | Description |
|---|---|---|---|
| E | Medium | `RtspSessionManager.cpp::OpenReaderLocked()` (~L153-157) | Strict check: if decoder cannot produce exactly the requested NV12 resolution, session fails with `MF_E_INVALIDMEDIATYPE`. Fragile with H.265 sources or cameras that report slightly different sizes. |

<details>
<summary>Fixed (kept for history)</summary>

| # | File / Location | Was |
|---|---|---|
| A | `MediaStream.cpp::SetStreamState()` | `RUNNING` now calls `Start(_currentType.get())` instead of `Start(nullptr)`; PAUSED→RUNNING works. |
| B | `Activator.cpp::Initialize()` | `Initialize()` is called properly and its `HRESULT` checked/logged; no more orphaned stale-`hr` check. |
| C | `RtspSessionManager.cpp::TryGetLatestFrame()` | Only grabs an `AddRef`'d `IMFSample` under `_lock`; the actual pixel copy happens in `MediaStream::CopyRtspFrame` outside any session lock. |
| D | `MediaStream.cpp::RequestSample()` | Sample duration is now `(10000000LL * _hintFpsDen) / max(1u, _hintFpsNum)`, not hardcoded to 30fps. |
| F | `VCamMediaSource.cpp::KsProperty()` | Handler now parses and logs the runtime RTSP URL property properly (no `#if 0`, no reference to a nonexistent `SetRTSPUrl`). Runtime hot-swap of the URL is still not implemented — config remains set at `Start()` time only — but the handler is no longer dead/broken code. |
| G | `PerformanceTests1/MediaStreamCopyPerfTests.cpp` | Rewritten as MF-infrastructure placeholder tests; no longer calls the removed `CopyRtspBufferToTargetSample`. The whole `PerformanceTests1` project was removed on 2026-07-09 (it was never wired into `RTVirtualCamera.sln` and only held placeholder tests) — see Testing section below. |
| H | `RTCamNative/VirtualCamera.cpp::StartVirtualCamera()` | The diagnostic `CoCreateInstance(CLSID_VCam, CLSCTX_INPROC_SERVER)` call is gone. |

</details>

---

## Dead code / to ignore

| Component | Notes |
|---|---|
| `FrameGenerator` (`VirtualCamera/`) | Never called in `RequestSample` main path. Synthetic frame fallback only (also shown during RTSP `Reconnecting`). |
| `VideoPlayer` / `VideoReaderCbk` / `CSourceOpenMonitor` | App-process preview only, not in Frame Server path. |
| `SetOutputMediaType()` in `RtspReaderCallback.cpp` | Public method, not called anywhere. |

---

## Testing

No automated tests currently in the repo. `PerformanceTests1/` (a placeholder MF-infrastructure unit test project, never wired into `RTVirtualCamera.sln`) was removed on 2026-07-09 as part of a repo cleanup. If `MediaStream::CopyRtspFrame` needs test coverage again, its NV12 copy logic will need extracting into a free function or a friend-test pattern first, since it's a private member.

No automated test for the Frame Server path — debug by attaching VS to `svchost.exe`.
