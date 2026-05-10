# RealTimeWebCam — RTSP to Virtual Camera

Zoom (e Teams, Skype, ecc.) non supporta sorgenti RTSP direttamente. Questo progetto risolve il problema creando una **virtual camera di Windows** che fa da proxy: apre il flusso RTSP di una camera di rete e lo presenta al sistema come se fosse una normale webcam USB.

Funziona su **Windows 11** grazie all'API [`MFCreateVirtualCamera`](https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera) introdotta con Windows 11 21H2.

Basato su [VCamSample di Simon Mourier](https://github.com/smourier/VCamSample) come punto di partenza per la parte Media Foundation.

## AOT version

> Questo progetto **non** usa la versione AOT. Il README originale del progetto di esempio da cui è partito menzionava AOT — non è rilevante qui.

## Come funziona

```
RTVirtualCamera.exe (C# WinForms)
  │  inserisce l'URL RTSP
  │  avvia/ferma la virtual camera
  ▼
MFPipeline.dll (C++ bridge)
  │  chiama MFCreateVirtualCamera()
  │  passa l'URL RTSP come attributo MF
  ▼
Windows Frame Server (svchost.exe)  ← processo separato di sistema
  │  carica VCamSampleSource.dll
  │  apre il flusso RTSP
  │  legge frame e li consegna alle app
  ▼
Zoom / Teams / qualsiasi app webcam
```

Il Frame Server è un servizio di sistema di Windows 11 che gestisce le camera. Carica la nostra DLL nel suo processo e la usa come sorgente video. Non c'è condivisione di frame tra processi: il Frame Server apre RTSP per conto suo.

---

## Progetti nella soluzione

### `VirtualCamera/` → `VCamSampleSource.dll`
La parte più importante. È una **COM DLL** che implementa `IMFMediaSourceEx`, l'interfaccia standard di Windows Media Foundation per le sorgenti video.

Va registrata nel sistema con `regsvr32` (come amministratore, in HKLM). Da quel momento Windows la conosce tramite il suo CLSID `{3CAD447D-F283-4AF4-A3B2-6F5363309F52}`.

Quando un'app apre la virtual camera, il **Frame Server** carica questa DLL nel suo processo e:
- `MediaSource::Initialize()` legge l'URL RTSP dagli attributi MF
- `MediaSource::Start()` lo passa agli stream
- `MediaStream::InitializeRTSPReader()` apre il flusso RTSP con `IMFSourceReader`
- `MediaStream::RequestSample()` legge ogni frame e lo consegna al Frame Server, che lo distribuisce a Zoom, Teams, ecc.

Se non c'è URL RTSP configurato, il frame è nero (fallback di sicurezza).

### `MFPipeline/` → `MFPipeline.dll`
**Bridge tra C# e le API Win32 di Media Foundation.** C# non può chiamare facilmente le API COM native di MF, quindi questa DLL C++ espone un'interfaccia semplice in stile C che il C# chiama via P/Invoke.

Contiene due componenti:

- **`VirtualCamera`** — chiama `MFCreateVirtualCamera()` con il CLSID di VCamSampleSource, fa start/stop/unregister, e imposta l'URL RTSP tramite `IMFVirtualCamera::AddProperty()`. È il "telecomando" della virtual camera.
- **`VideoPlayer`** — apre un file video o stream RTSP con `IMFSourceReader` e lo visualizza in un pannello WinForms tramite EVR (Enhanced Video Renderer). Serve solo per il **preview nell'UI**, non è coinvolto nel flusso verso Zoom.

### `RTVirtualCamera/` → `RTVirtualCamera.exe`
**Applicazione di controllo** in C# WinForms (namespace `TestVideo`).

- Permette di inserire l'URL RTSP della camera di rete
- Avvia/ferma la virtual camera tramite `VirtualCameraWrapper` (P/Invoke su `MFPipeline.dll`)
- Mostra un preview del flusso nell'UI tramite `VideoPlayerWrapper`

---

## Setup e utilizzo

### Prerequisiti
- Windows 11 21H2 o successivo
- Visual Studio 2022

### Build
Aprire `RTVirtualCamera.sln` in Visual Studio 2022 e compilare in **x64**.

I binari prodotti sono tutti nella stessa cartella nella root della soluzione:

| File | Cartella |
|---|---|
| `VCamSampleSource.dll` | `bin\x64\Debug\` oppure `bin\x64\Release\` |
| `MFPipeline.dll` | `bin\x64\Debug\` oppure `bin\x64\Release\` |
| `RTVirtualCamera.exe` | `bin\x64\Debug\` oppure `bin\x64\Release\` |

Tutti e tre i binari finiscono nella stessa cartella: l'exe trova automaticamente `MFPipeline.dll` senza copiare nulla.

### Registrazione di VCamSampleSource.dll

Va eseguita **una volta** (o ad ogni ricompilazione durante lo sviluppo). Lo script `VirtualCamera\deploy_vcam.ps1` automatizza il processo: ferma il Frame Server, ri-registra la DLL, riavvia il Frame Server. Si auto-eleva ad Administrator.

```powershell
.\VirtualCamera\deploy_vcam.ps1 -DllPath ".\bin\x64\Debug\VCamSampleSource.dll"
```

Per automatizzarlo ad ogni build, aggiungere nelle proprietà del progetto `VirtualCamera` → **Build Events → Post-Build Event**:

```
powershell -NoProfile -ExecutionPolicy Bypass -File "$(ProjectDir)deploy_vcam.ps1" -DllPath "$(TargetPath)"
```

> La DLL deve trovarsi in una cartella accessibile a tutti gli utenti (es. `C:\VCam\`), perché il Frame Server gira come *Local Service* e non ha accesso alle cartelle utente. La cartella `bin\x64\` nella root del repo va bene se il repo è in una cartella pubblica (es. `C:\Projects\`). Se il repo è sotto `C:\Users\...`, spostare la DLL prima di registrarla.

### Avvio
1. Eseguire `RTVirtualCamera.exe`
2. Inserire l'URL RTSP della camera (es. `rtsp://192.168.1.10:554/stream`)
3. Cliccare **Start VCam**
4. La virtual camera "RTSP Virtual Camera" apparirà in Zoom, Teams, ecc.

---

## Troubleshooting

### La virtual camera non appare o mostra frame neri
- Verificare che `VCamSampleSource.dll` sia registrata (`regsvr32`)
- Verificare che la DLL sia in una cartella accessibile a tutti (non sotto `C:\Users\...`)
- Controllare che l'URL RTSP sia raggiungibile dalla macchina Windows

### "Access Denied" su `IMFVirtualCamera::Start`
Il Frame Server (`svchost.exe`) non riesce ad accedere alla DLL. Spostare la DLL in una cartella pubblica (es. `C:\VCam\`) e ripetere la registrazione da lì.

### Debug del Frame Server
Il Frame Server è un processo separato. Per debuggarlo con Visual Studio: **Attach to Process** → cercare `svchost.exe` che ospita il servizio *Windows Camera Frame Server*.

---

## Note di sviluppo
- Sviluppo su Linux via Samba, compilazione su guest Windows con Visual Studio 2022
- Il CLSID `{3CAD447D-F283-4AF4-A3B2-6F5363309F52}` deve corrispondere ovunque (VirtualCamera DLL, MFPipeline, registro di sistema)
- I formati video supportati sono RGB32 e NV12; il descrittore dello stream è attualmente hardcoded a 1280×960 @ 30fps

