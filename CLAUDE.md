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

The Frame Server (`svchost.exe`) is a separate OS process. Attach to it in Visual Studio: **Attach to Process** → find `svchost.exe` hosting the *Windows Camera Frame Server* service. Use `WINTRACE` / `DebugLog` macros for trace output — do not rely on the Visual Studio Output window for Frame Server behavior. The FFmpeg producer runs in the **app** process (`RTVirtualCamera.exe` / `RTCamNative.dll`), so debug the receive/decode side there, not in `svchost.exe`.

**Both trace paths are disabled by default** (production pays no formatting / file-I/O cost) and gated at runtime by an environment variable, read once and cached per process:

- `WINTRACE` (Frame Server ETW): enabled by the **machine** env var `RTVCAM_TRACE=1` (Local Service doesn't inherit user variables; `setx /M`, then restart the Frame Server). Gated in `WinTrace.h` (`WinTraceEnabled()`), which also skips `EventRegister` when off.
- `DebugLog` (app file log): enabled by `RTVCAM_LOG=1` for `RTVirtualCamera.exe`. Gated in `RTCamNative/Logger.cpp` (`DebugLogEnabled()`).

Any value other than `0`/empty enables it.

---

## Architecture

This project makes an RTSP network camera appear as a standard Windows webcam for Zoom, Teams, etc.

**Single receive engine (FFmpeg, user-space).** The app decodes the RTSP stream in its own
process with libav (GPU via d3d11va, else software), converts to NV12, and pushes frames to the
Frame Server through a shared-memory frame channel. The Frame Server never opens the RTSP source
itself — it only re-serves those frames to the consumer through the required Media Foundation
virtual-camera interface. (An earlier Media Foundation receive engine — `RtspSessionManager` +
`IMFSourceReader` inside the Frame Server — was removed; only its shape survives in the
"service creates the mapping, app opens it" privilege pattern.)

```
RTVirtualCamera.exe (C# WinForms, .NET 10)
  ├─ Preview:  VideoPlayerWrapper → RTCamNative!FfmpegPreviewPlayer
  │              └─ FfmpegRtspSource (libav decode) → NV12 → BGRA → GDI blit into the panel
  └─ Virtual camera:
       └─ P/Invoke → RTCamNative.dll (C++/CLI bridge)
            └─ MFCreateVirtualCamera(CLSID_VCam)
            └─ IMFVirtualCamera::SetString/SetUINT32(...)   ← config attrs, BEFORE Start()
            └─ IMFVirtualCamera::Start()
       └─ after Start(): VCam_StartFfmpegProducer(url,w,h,…)  [FfmpegExports.cpp]
            └─ FfmpegRtspSource (libav decode) → NV12
                 └─ FrameChannelWriter → Global\RTVCam_Frames_<CLSID> (shared memory)

Windows Frame Server (svchost.exe)  ← separate OS process, Local Service
  └─ loads VCamSampleSource.dll (COM DLL, HKLM registered)
       └─ Activator::ActivateObject()
            └─ VCamMediaSource::SetupCameraSettings(attrs) ← reads geometry; CREATES the frame mapping
            └─ VCamMediaSource::Initialize(attrs)          ← creates streams/descriptors
       └─ MediaStream::RequestSample() ~30x/sec
            └─ CopyFrameChannelFrame() → FrameChannelReader::AcquireLatest() → CopyNv12ToSample()
            └─ FrameGenerator synthetic frame if the producer heartbeat is stale (>2s) or none yet
            └─ MediaStream::PublishStats() → StatsPublisher (Global\ shared memory)

RTVirtualCamera.exe (UI timer, ~2x/sec)
  └─ RTCamNative::StatsReader::TryGetStats() ← reads the stats shared memory
```

Two shared-memory channels, both `Global\` + explicit DACL (service creates, app opens):

- **Frame channel** (`Global\RTVCam_Frames_<CLSID>`, `Shared/VCamFrameChannel.h`) — NV12 pixels,
  app → Frame Server. Created by the Frame Server (`FrameChannelReader`), written by the app
  (`FrameChannelWriter`).
- **Stats channel** (`Global\RTVCam_Stats_<CLSID>`, `Shared/VCamStats.h`) — live fps/copy-cost,
  Frame Server → app. Created/written by the Frame Server (`StatsPublisher`), read by the app
  (`StatsReader`).

**App-lifetime producer.** Because the decode runs in the app process, the camera only has live
frames while `RTVirtualCamera.exe` is running; on app close the Frame Server shows the synthetic
`FrameGenerator` frame (the producer heartbeat goes stale). This is inherent to decoding in user
space and is an accepted trade-off.

### Virtual camera lifecycle (detailed)

1. App calls `MFCreateVirtualCamera()` — registers the camera in the system, DLL not yet loaded.
2. App calls `IMFVirtualCamera::SetString(MF_VCAM_RTSP_URL, url)` and `SetUINT32(...)` for width/height/fps — stored in the MF attributes bag.
3. App calls `IMFVirtualCamera::Start()` — Frame Server loads the DLL, calls `Activator::ActivateObject()` → `SetupCameraSettings()` (which **creates** the frame mapping) → `Initialize()`.
4. App calls `VCam_StartFfmpegProducer(url,w,h,…)` — the user-space `FfmpegRtspSource` opens the RTSP URL, decodes, and starts writing NV12 into the frame mapping via `FrameChannelWriter` (retrying `OpenFileMappingW` until the mapping exists).
5. When a consumer (Zoom) opens the camera, `MediaStream::RequestSample()` is called ~30x/sec; it reads the latest frame from `FrameChannelReader` and copies it into the allocated sample (or a synthetic frame if the producer heartbeat is stale).
6. On stop: `VCam_StopFfmpegProducer()` joins the decode thread; `VCamMediaSource::Stop()`/`Shutdown()` tear down the streams.

---

## Key classes

### `VirtualCamera/` (COM DLL — runs in Frame Server process)

| Class | File | Role |
|---|---|---|
| `Activator` | `Activator.cpp` | `IMFActivate` — entry point when Frame Server loads the DLL. Calls `SetupCameraSettings()` then `Initialize()`. |
| `VCamMediaSource` | `VCamMediaSource.cpp/.h` | `IMFMediaSourceEx` — the camera source. Reads config attrs, **creates the frame mapping** (`FrameChannelReader::EnsureMapped`), owns stream lifecycle. Never opens RTSP. |
| `MediaStream` | `MediaStream.cpp/.h` | `IMFMediaStream2` — one video stream. On `RequestSample()` pulls the latest NV12 frame from `FrameChannelReader` via `CopyFrameChannelFrame()`, copies it with `CopyNv12ToSample()`, and delivers it (re-serving the same frame if the producer hasn't advanced, tracked as `declinedFrames`, count-only). Falls back to `FrameGenerator` before the first frame or when the producer heartbeat is stale. |
| `FrameChannelReader` | `FrameChannelReader.cpp/.h` | Singleton (`Instance()`). **Creates** `Global\RTVCam_Frames_<CLSID>` with an explicit DACL (service privilege), max-sized (3840×2160) so it survives resolution changes. `AcquireLatest()` reads the latest ring slot under the header seqlock. |
| `FrameGenerator` | `FrameGenerator.cpp` | Synthetic-frame fallback (Direct2D). Shown before the first real frame and whenever the producer heartbeat is stale ("Camera IP non connessa"). |
| `StatsPublisher` | `StatsPublisher.cpp/.h` | Singleton (`Instance()`). Writes `VCamFrameServerStats` into the stats shared memory on every `MediaStream::RequestSample()` tick. See "Live stats" below. |
| `CameraSessionConfig` | `CameraSessionConfig.h` | Plain struct (geometry/identity) applied to each `MediaStream` via `StreamRuntimeContext`. |

### `RTCamNative/` (C++/CLI bridge DLL — runs in app process)

| Class | File | Role |
|---|---|---|
| `VirtualCamera` | `VirtualCamera.cpp/.h` | Wraps `IMFVirtualCamera`. Calls `SetString`/`SetUINT32` on the attrs bag before `Start()`. Exports C-style functions for P/Invoke. |
| `FfmpegRtspSource` | `FfmpegRtspSource.cpp/.h` | **Native** (non-/clr). The single RTSP receiver: libav open/decode (d3d11va GPU or software), latency cap + resync-to-live, `sws_scale` → NV12, hands each frame to a caller-supplied `FrameSink`. Reconnects on its own until `Stop()`. Also `Probe()` (geometry/codec) as a static. |
| `FfmpegExports.cpp` | — | C exports for the virtual-camera producer (`VCam_StartFfmpegProducer` etc.). Owns a `FrameChannelWriter`; its sink writes NV12 into the frame shared memory. |
| `FfmpegPreviewPlayer` | `FfmpegPreviewPlayer.cpp/.h` | **Native**. FFmpeg-based preview (replaces the old MF/EVR `VideoPlayer`). Reuses `FfmpegRtspSource`; its sink converts NV12→BGRA and blits into the WinForms panel HWND with a double-buffered GDI path. Keeps `PreviewStats`. |
| `PreviewExports.cpp` | — | C exports the C# preview wrapper P/Invokes (`CreateVideoPlayer`/`SetVideoPath`/`PlayVideo`/`GetVideoStreamInfo`/…), implemented on `FfmpegPreviewPlayer` — same names/signatures as before, so the managed side is unchanged. |
| `FrameChannelWriter` | `FrameChannelWriter.cpp/.h` | **Native**. Opens the frame mapping (created by the Frame Server) for writing, publishes NV12 into the ring slot under the header seqlock. |
| `StatsReader` | `StatsReader.cpp/.h` | Singleton (`Instance()`). Opens the stats shared memory (read-only) and exports `VCam_GetFrameServerStats()` for P/Invoke. |

**`/clr` note:** RTCamNative is compiled `/clr` (mixed-mode) on x64, and the libav C headers don't mix with C++/CLI. So `FfmpegRtspSource.cpp`, `FfmpegPreviewPlayer.cpp`, `FfmpegExports.cpp`, `PreviewExports.cpp`, and `FrameChannelWriter.cpp` are compiled **native** (`CompileAsManaged=false`, no PCH). Their C exports are the boundary the managed wrappers call.

### `RTVirtualCamera/` (C# WinForms, namespace `RTVirtualCamera`)

| Class | File | Role |
|---|---|---|
| `VirtualCameraWrapper` | `VirtualCameraWrapper.cs` | P/Invoke façade over `RTCamNative.dll`. Declares the C# mirror of `VCamConfig` and of `VCamFrameServerStats` as `FrameServerStats`. `SetConfig → Register → Start`, then `StartFfmpegProducer`. `TryGetFrameServerRates()` polls `VCam_GetFrameServerStats`. |
| `VideoPlayerWrapper` | `VideoPlayerWrapper.cs` | P/Invoke façade for the FFmpeg preview player. Mirrors `StreamInfo` and `PreviewStats`. |
| `MainForm` | `MainForm.cs` | Main UI. Probes source, builds `VCamConfig`, `SetConfig → Register → Start → StartFfmpegProducer`. `StatsTimer_Tick` shows preview stats normally, or Frame-Server stats once the virtual camera is running. |
| `Settings` | `Settings.cs` | Persists `RtspURL` and `AutoStart`. |

### `Shared/VCamConfig.h`

Single source of truth for the GUIDs and the `VCamConfig` struct shared between `RTCamNative/` and `VirtualCamera/`. Included by both C++ projects. **The C# mirror `VirtualCameraWrapper.cs::VCamConfig` must match byte-for-byte.**

### `Shared/VCamFrameChannel.h`

Single source of truth for the FFmpeg frame channel wire format: the `VCamFrameChannelHeader` struct, the `Global\RTVCam_Frames_<CLSID>` mapping name, the triple-buffered NV12 ring, and the seqlock convention. Included by `RTCamNative/FrameChannelWriter.cpp` (writer) and `VirtualCamera/FrameChannelReader.cpp` (reader). No C# mirror — the app touches this channel only through native code.

### `Shared/VCamStats.h`

Single source of truth for the live-stats wire format: `VCamFrameServerStats`, the `VCAM_STATS_MAPPING_NAME` shared-memory name, and the seqlock convention. Included by `VirtualCamera/StatsPublisher.cpp` (writer) and `RTCamNative/StatsReader.cpp` (reader). **The C# mirror `VirtualCameraWrapper.cs::FrameServerStats` must match byte-for-byte** (`Pack = 1`, same field order).

---

## The FFmpeg receive path

```
RTVirtualCamera.exe (app process)
  MainForm: after Start(), VirtualCameraWrapper.StartFfmpegProducer(url,w,h,…)
    → RTCamNative!VCam_StartFfmpegProducer  (FfmpegExports.cpp)
      → FfmpegRtspSource (thread): libavformat open RTSP → libavcodec decode (d3d11va or SW)
        → libswscale → NV12 → sink → FrameChannelWriter.WriteFrame()
          → seqlock publish into Global\RTVCam_Frames_<CLSID>  (Shared/VCamFrameChannel.h)

Frame Server (svchost.exe)
  VCamMediaSource::SetupCameraSettings
    → FrameChannelReader::EnsureMapped(w,h)   ← CREATES the mapping (service privilege)
  MediaStream::RequestSample (~30x/sec)
    → CopyFrameChannelFrame() → FrameChannelReader::AcquireLatest() → CopyNv12ToSample()
    → synthetic FrameGenerator frame if the producer heartbeat is stale (>2s) or no frame yet
```

Design points:

- **Producer lives in the app process → app-lifetime.** The camera only has live frames while
  `RTVirtualCamera.exe` runs; on app close the Frame Server shows the synthetic frame.
- **The frame mapping is CREATED by the Frame Server, WRITTEN by the app** — creating a `Global\`
  object needs `SeCreateGlobalPrivilege` (Local Service has it; a non-elevated user does not). DACL
  `D:(A;;GA;;;LS)(A;;GRGW;;;IU)` grants Interactive Users read+write. See
  `FrameChannelReader::EnsureMapped` / `BuildFramesSecurityDescriptor`.
- **Fixed max-size mapping.** Created for `VCAM_FRAMES_MAX_*` (3840×2160); real geometry lives in
  the header, so it survives resolution changes across sessions without a resize.
- **Triple-buffered, seqlock-published.** Ring of `VCAM_FRAMES_SLOT_COUNT` (3) NV12 slots; the
  single writer fills the next slot then publishes `latestSlot`+`frameSeq` under the header seqlock.
- **FFmpeg = LGPL, dynamic, via vcpkg.** Provided by vcpkg manifest mode; vcpkg is a git submodule
  at `external/vcpkg`, and `vcpkg.json` pins the version via `builtin-baseline` (ffmpeg 8.1.2, LGPL,
  features `avcodec`/`avformat`/`swscale`). `RTCamNative.vcxproj` imports vcpkg's MSBuild
  props/targets; vcpkg app-locally copies the DLLs into the shared `bin` and the WiX `<Files>` glob
  harvests them into the MSI. `THIRD_PARTY_NOTICES.md` covers the LGPL notice.

### Latency handling (in `FfmpegRtspSource`)

The decode loop opens RTSP with low-latency demux options (`rtsp_transport=tcp`,
`reorder_queue_size=0`, `max_delay=0`, `fflags=nobuffer+discardcorrupt`, `flags=low_delay`,
`avioflags=direct`, `analyzeduration=0`), and the decoder with `AV_CODEC_FLAG_LOW_DELAY` +
`FF_THREAD_SLICE`. Hardware decode (`d3d11va`) is attached when available; GPU frames are
downloaded to system-memory NV12 for the channel. A **latency cap** anchors a wall-clock ↔ PTS
baseline and, when a decoded frame falls more than `kMaxLagMs` (350 ms) behind live, resyncs by
dropping the rest of the GOP (skip to the next keyframe) and flushing the decoder. This is what
keeps a fast/misbehaving source from accumulating unbounded latency — the failure mode that a
previous MF-based preview exhibited (reading ~130 fps from a 30 fps source with growing drift).

The same `FfmpegRtspSource` core serves both the preview and the virtual-camera producer, each
with its own `FrameSink`; they never run at the same time (the preview stops when the camera
starts), so only one RTSP connection is open at a time. Reconnection is built into the decode
loop: on a stream break it sleeps 1s and retries `avformat_open_input` until `Stop()`.

---

## Frame copy path (Frame Server side)

The frame reaches the consumer's sample through `MediaStream::CopyFrameChannelFrame`:

- `FrameChannelReader::AcquireLatest` returns a pointer to the latest NV12 ring slot (bounded
  seqlock read) plus geometry, `frameSeq`, and the producer's heartbeat tick.
- Freshness: the producer stamps `GetTickCount64()` on every write; >2s without an update ⇒
  "producer gone" ⇒ `S_FALSE`, and `RequestSample` falls back to the synthetic frame.
- `CopyNv12ToSample` does a single `MFCopyImage` per plane (Y + interleaved UV) from the
  system-memory slot into the destination sample buffer, honoring the dest's real pitch (2D or
  flat). The copy is always CPU — the source is system memory, not a GPU texture. (The Frame
  Server's D3D11 device manager, from `SetD3DManager`, is still used for the *output* sample
  allocator, but there is no source-side GPU copy anymore.)

## Live stats (Frame Server → app)

```
MediaStream::RequestSample() (~30x/sec, Frame Server)
  → assembles a VCamFrameServerStats snapshot:
       sessionState   ← Running/Starting from the producer heartbeat freshness (_ffmpegFresh)
       rxFrames       ← the producer's framesWritten counter (frame-channel header)
       renderedFrames/declinedFrames/lastCopyMs ← tracked in MediaStream itself
       hwAccelCapable/hwAccelActive ← always 0 (the Frame Server copy is always CPU)
  → StatsPublisher::Publish(snapshot)  → seqlock write into Global\RTVCam_Stats_<CLSID>

MainForm.StatsTimer_Tick (~2x/sec, app UI thread)
  → VirtualCameraWrapper.TryGetFrameServerRates()
       → RTCamNative!VCam_GetFrameServerStats() → StatsReader::TryGetStats()  (seqlock read)
       → derives fps from counter deltas across two polls
```

Notes:

- **The real GPU/CPU decode signal is app-side**, not in the Frame Server stats: the label uses
  `VCam_IsFfmpegProducerHardware()` (whether the producer's last frame came off d3d11va) to show
  `FFmpeg HW` / `FFmpeg SW`. `hwAccelCapable`/`hwAccelActive` in `VCamFrameServerStats` are always
  0 now (they tracked the removed in-process GPU zero-copy path); the fields stay for wire
  compatibility.
- **Seqlock, not a `Mutex`.** The writer runs on the latency-critical `RequestSample` path;
  `sequence` is bumped odd→even around the writes (`InterlockedIncrement`), the reader retries
  (bounded) on an odd/changed sequence. Exactly one writer makes this safe.
- **Staleness.** `updatedTickMs` is `GetTickCount64()` at the last publish;
  `TryGetFrameServerRates` flags `Stale` once it hasn't advanced for >3s.
- **`renderedFrames` vs `declinedFrames` vs `rxFrames`.** `MediaStream::RequestSample` always
  copies and delivers the latest slot, even when `frameSeq` matches the last delivery (a re-serve
  because the consumer polls faster than the producer writes). `renderedFrames` counts every
  delivery; `declinedFrames` counts the same-`frameSeq` re-serves (diagnostics only, no behavior).
  An earlier attempt to actually decline re-serves via `MF_E_SAMPLEALLOCATOR_EMPTY` destabilized
  the Frame Server's retry timing and was reverted.
- **The mapping outlives any single camera session.** `StatsPublisher` is a process-wide singleton;
  a finished session just stops advancing `updatedTickMs`.

---

## Critical invariants

- **CLSID** `{3CAD447D-F283-4AF4-A3B2-6F5363309F52}` must match in `VirtualCamera/`, `RTCamNative/`, and the registry. Never change it in only one place.
- **Attribute GUIDs** (`MF_VCAM_RTSP_URL`, `MF_VCAM_WIDTH`, `MF_VCAM_HEIGHT`, `MF_VCAM_FPS_NUM`, `MF_VCAM_FPS_DEN`) are defined in `Shared/VCamConfig.h` and included by both C++ projects — single definition.
- **Config attrs must be set BEFORE `IMFVirtualCamera::Start()`**. The Frame Server reads them in `Activator::ActivateObject()` → `SetupCameraSettings()`. Attributes set after `Start()` are not re-read.
- **`SetBlob` is NOT reliably forwarded** by the Frame Server to `Initialize()`. Only `SetString` and `SetUINT32` are safe — hence individual attribute calls.
- **DLL path constraint**: `VCamSampleSource.dll` must be in a publicly accessible folder (not under `C:\Users\...`), because the Frame Server runs as Local Service.
- **`Activator::ActivateObject()` call order**: `SetupCameraSettings()` before `Initialize()` — `Initialize()` calls `ApplyRuntimeContext()` which reads `_camera_config` filled by `SetupCameraSettings()`.
- **The virtual camera stays Media Foundation.** Only the source of the pixels changed (the FFmpeg frame channel). Do not reintroduce an in-process RTSP reader in the Frame Server.
- **`VCamConfig` struct ABI**: when modifying `Shared/VCamConfig.h::VCamConfig`, update the C# mirror `RTVirtualCamera/VirtualCameraWrapper.cs::VCamConfig` in the same change.
- **`VCamFrameChannel` wire format**: `Shared/VCamFrameChannel.h` is the single source of truth; writer (`RTCamNative/FrameChannelWriter.cpp`) and reader (`VirtualCamera/FrameChannelReader.cpp`) both include it. Bump `VCAM_FRAMES_STRUCT_VERSION` if the layout changes. No C# mirror.
- **The frame mapping is created ONLY by the Frame Server** (`FrameChannelReader`, `SeCreateGlobalPrivilege`); the app opens it for writing. Do not create it app-side.
- **`VCamFrameServerStats` struct ABI**: when modifying `Shared/VCamStats.h::VCamFrameServerStats`, bump `VCAM_STATS_STRUCT_VERSION` and update `RTVirtualCamera/VirtualCameraWrapper.cs::FrameServerStats` (same field order, `Pack = 1`) in the same change.
- **Both shared mappings use the `Global\` namespace with an explicit DACL** — the Frame Server (Local Service, session 0) and the app (interactive user session) are in different Terminal Server sessions. Handled in `FrameChannelReader::EnsureMapped` / `StatsPublisher::EnsureMapped`; do not create either mapping anywhere else.
- **libav files are native (non-/clr).** RTCamNative is `/clr`; the FFmpeg/preview translation units must stay `CompileAsManaged=false` with no PCH, or the libav C headers won't compile.

---

## Dead code / to ignore

| Component | Notes |
|---|---|
| `FrameGenerator` (`VirtualCamera/`) | Synthetic-frame fallback only (before the first frame, or when the producer heartbeat is stale). |

---

## Testing

No automated tests currently in the repo. No automated test for the Frame Server path — debug by attaching VS to `svchost.exe`; debug the FFmpeg receive/decode path in the app process (`RTVirtualCamera.exe`).
