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

## Architecture

```
RTVirtualCamera.exe (C# WinForms)
  └─ P/Invoke → RTCamNative.dll (C++)
       ├─ VideoPlayer  → PREVIEW in the UI (EVR). Software decode, in the app process.
       └─ VirtualCamera → MFCreateVirtualCamera() + RTSP attributes, then Start()

Windows Frame Server (svchost.exe)  ← separate process, Local Service
  └─ loads VirtualCamera.dll (COM, registered in HKLM)
       └─ opens the RTSP with IMFSourceReader (GPU/DXVA decode, zero-copy)
       └─ delivers frames to Zoom/Teams/…
```

Two **independent** paths that each open the RTSP on their own (no shared memory between processes):

- **Preview** (in the app): software decode + EVR. Convenient but heavier; at high framerates it drops frames to stay responsive. This is what the diagnostics bar measures.
- **Frame Server** (for Zoom): hardware DXVA decode with a GPU→GPU copy, always keeping only the latest frame. This is the one that matters for video calls.

Code-level details (key classes, critical invariants, frame-copy path, reconnection, known bugs): see [`CLAUDE.md`](CLAUDE.md).

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

---

## Credits and license

Builds on [**VCamSample** by Simon Mourier](https://github.com/smourier/VCamSample) for the Media Foundation scaffolding.

**MIT** license (see [`LICENSE`](LICENSE)), with copyright to Simon Mourier (original) and Andrea Greco (rewrite).
