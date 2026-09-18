# Development

Build, run and internals for **RealTimeWebCam**. For installing and using the app, see [`README.md`](README.md). For a deeper, code-level guide (key classes, invariants, known bugs) see [`CLAUDE.md`](CLAUDE.md).

---

## Build from source

### Prerequisites
- Visual Studio 2022 (C++ desktop + .NET desktop workloads)
- WiX Toolset (for the `Setup` project)
- Windows 11 21H2+

### Compile
Open `RTVirtualCamera.sln` and build in **x64**. The three binaries land in the same folder (`bin\x64\Debug\` or `bin\x64\Release\`):

| Binary | Project |
|---|---|
| `VirtualCamera.dll` | `VirtualCamera/` (COM source, runs in the Frame Server) |
| `RTCamNative.dll` | `RTCamNative/` (C++↔C# bridge, preview) |
| `RTVirtualCamera.exe` | `RTVirtualCamera/` (WinForms UI) |

`Setup/` produces `RT-VirtualCam-Setup.msi`.

There is no CLI build command; this is a Visual Studio solution only. Target: `.NET Framework 4.8` (C#), C++17 (C++), all x64.

### Registering the COM DLL (development only)

After every rebuild, register `VirtualCamera.dll`. The script self-elevates, stops the Frame Server, re-registers and restarts it:

```powershell
.\VirtualCamera\deploy_vcam.ps1 -DllPath ".\bin\x64\Debug\VirtualCamera.dll"
```

The script copies the DLL to `C:\Projects\RTVirtualCamera` before registering (Local Service can't access `C:\Users\...`); `-DeployDir` overrides this. To clean up between tests: `.\VirtualCamera\unregister_vcam.ps1`.

> **The DLL must live in a folder accessible to everyone** (not under `C:\Users\...`), because the Frame Server runs as *Local Service*. A folder under `C:\Projects\` works; the user profile causes `E_ACCESSDENIED` on `IMFVirtualCamera::Start`. The MSI, installing into `Program Files`, avoids this by design.

---

## Create an RTSP source from a USB webcam

If you don't have an RTSP camera but want to test with a plain **USB webcam** (e.g. on a Linux mini-PC / Raspberry Pi), the proven recipe is **MediaMTX + FFmpeg** via Docker.

> 💡 **Encode for low latency at the source.** The app receives and decodes the stream with **FFmpeg** in its own process, so it reassembles frames robustly — the old requirement to force a single H.264 slice per frame is **gone** (multi-slice streams now decode correctly, exactly as `ffplay` always did). What still matters for a real-time picture is encoding without buffering: `-tune zerolatency`, a baseline profile (no B-frames), and a keyframe roughly every second. The recipe below already does all of this.

### docker-compose

```yaml
services:
  rtsp-server:
    image: bluenviron/mediamtx
    restart: unless-stopped
    ports:
      - "8554:8554"   # RTSP
      - "1935:1935"   # RTMP (optional)
      - "8888:8888"   # HLS (optional)
    environment:
      - MTX_PROTOCOLS=tcp

  webcam:
    image: linuxserver/ffmpeg:latest
    restart: unless-stopped
    depends_on:
      - rtsp-server
    devices:
      - /dev/video0:/dev/video0
    command: >-
      -fflags nobuffer -flags low_delay -probesize 32 -analyzeduration 0
      -f v4l2 -input_format mjpeg -framerate 30 -video_size 1280x720
      -thread_queue_size 512 -i /dev/video0
      -c:v libx264 -preset ultrafast -tune zerolatency
      -x264-params sliced-threads=0
      -profile:v baseline -pix_fmt yuv420p
      -g 30 -sc_threshold 0
      -crf 23 -maxrate 6000k -bufsize 2000k
      -an
      -f rtsp -rtsp_transport tcp
      rtsp://rtsp-server:8554/webcam
```

The URL to enter in the app will be `rtsp://<server-IP>:8554/webcam`.

### Golden rules for low latency

- **`-tune zerolatency` + `-profile:v baseline`** — no B-frames, no lookahead.
- **One honest framerate.** No `-vf fps=N` upsampling from a capture running at a different rate: it invents synthetic timestamps and adds jitter. Capture and stream at the same real rate (if the webcam only does 15 fps, stream 15).
- **`-g 30`** — a keyframe every second (fast recovery on connect and after packet loss).
- **`-x264-params sliced-threads=0`** — no longer required (FFmpeg reassembles multi-slice frames correctly); harmless to leave in.
- **Transport.** `MTX_PROTOCOLS=tcp` is rock-solid on a LAN. The app itself defaults to **UDP with automatic TCP fallback** and lets you force UDP-only or TCP-only in *Settings → Network* — the connection panel then shows which transport is actually carrying frames.

### Verify

On the server, before moving to Windows:

```bash
ffplay -fflags nobuffer -flags low_delay -framedrop -rtsp_transport tcp \
       rtsp://localhost:8554/webcam
```

It should be practically real-time and report the expected framerate (e.g. `30 fps`).

---

## Architecture

```
RTVirtualCamera.exe (C# WinForms)
  └─ P/Invoke → RTCamNative.dll (C++/CLI)
       ├─ FfmpegPreviewPlayer → PREVIEW in the UI (libav decode → GDI blit into the panel)
       └─ VirtualCamera       → MFCreateVirtualCamera() + RTSP attributes, then Start()
       └─ FfmpegRtspSource (producer) → decodes RTSP (d3d11va/SW) → NV12
            → FrameChannelWriter → Global\RTVCam_Frames_<CLSID> (shared memory)

Windows Frame Server (svchost.exe)  ← separate process, Local Service
  └─ loads VirtualCamera.dll (COM, registered in HKLM)
       └─ FrameChannelReader: CREATES the frame mapping (service privilege)
       └─ MediaStream::RequestSample → reads the latest NV12 frame → delivers to Zoom/Teams/…
       └─ synthetic frame when the app producer's heartbeat is stale
```

A **single** FFmpeg receive path: the app decodes the RTSP stream in user space (preview and
virtual-camera producer share the same `FfmpegRtspSource` decode core, one connection at a time)
and pushes NV12 frames to the Frame Server through the frame shared memory. The Frame Server never
opens the RTSP source itself. Because the decode lives in the app process, the camera is live only
while the app is running.

Code-level details (key classes, critical invariants, frame-copy path, latency handling, live
stats): see [`CLAUDE.md`](CLAUDE.md).

---

## Versioning

The versions of **all** components and of the MSI are derived from **git**, regenerated on every build by [`build/Set-GitVersion.ps1`](build/Set-GitVersion.ps1):

- with a `vX.Y.Z` tag (+N commits after) → `X.Y.Z` (MSI) / `X.Y.Z.N` (files);
- with no tag → `0.1.<commit-count>`;
- the informational string carries the short SHA and `-dirty`.

For a clean release just tag: `git tag v1.0.0` → the next build propagates `1.0.0` everywhere (exe, DLL, MSI). Generated files (`Version.g.*`) are not versioned.

---

## Troubleshooting (development)

**`E_ACCESSDENIED` / "Access Denied" on Start.** `VirtualCamera.dll` is under `C:\Users\...`: move it to a public folder and re-register. Doesn't happen with the MSI.

**`LNK1104: cannot open VirtualCamera.dll` when rebuilding.** The Frame Server holds the DLL open. Close the apps using the camera, or run `unregister_vcam.ps1` before the rebuild.

**Debugging the Frame Server.** It's `svchost.exe`: in Visual Studio *Attach to Process* → the instance hosting *Windows Camera Frame Server*. Traces use `WINTRACE` (an ETW provider, visible with TraceSpy).

**Diagnostics are off by default.** Both trace paths are gated at runtime and disabled in production (they cost formatting / file I/O per call). Enable them only when debugging:

- `WINTRACE` (Frame Server ETW, `VirtualCamera`): set the **machine** environment variable `RTVCAM_TRACE=1` (`setx /M RTVCAM_TRACE 1`), then restart the Frame Server so `svchost.exe` re-reads the environment. Local Service does not inherit user-level variables, so it must be machine-level.
- `DebugLog` (app file log at `%LOCALAPPDATA%\RTVirtualCamera\debug.log`, `RTCamNative`): set `RTVCAM_LOG=1` for the `RTVirtualCamera.exe` process (a user-level variable is fine).

Each value is read once and cached; a restart of the relevant process picks up a change. Any value other than `0`/empty enables it.

---

## Credits and license

Builds on [**VCamSample** by Simon Mourier](https://github.com/smourier/VCamSample) for the Media Foundation scaffolding.

**MIT** license (see [`LICENSE`](LICENSE)), with copyright to Simon Mourier (original) and Andrea Greco (rewrite).
