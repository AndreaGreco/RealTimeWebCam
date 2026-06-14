# RealTimeWebCam — da RTSP a webcam virtuale (Windows 11)

Zoom, Teams, Meet, Skype & co. **non** accettano sorgenti RTSP. Questo progetto colma il vuoto: registra in Windows una **webcam virtuale** che fa da proxy verso una telecamera IP. Apre il flusso RTSP e lo presenta al sistema come se fosse una normale webcam USB, utilizzabile da qualsiasi applicazione.

Funziona su **Windows 11** (21H2 o successivo) grazie all'API [`MFCreateVirtualCamera`](https://learn.microsoft.com/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera).

> Riscrittura che **parte da** [VCamSample di Simon Mourier](https://github.com/smourier/VCamSample): il merito dell'impianto Media Foundation per la virtual camera è suo. Da lì il progetto è stato riscritto e orientato all'uso RTSP a bassa latenza, con installer e UI dedicati. MIT, come l'originale.

---

## Indice

1. [Cosa fa e cosa serve](#cosa-fa-e-cosa-serve)
2. [Installazione (MSI)](#installazione-msi)
3. [Configurare la sorgente RTSP](#configurare-la-sorgente-rtsp) ← la parte che conviene leggere
4. [Usare l'applicazione](#usare-lapplicazione)
5. [Build da sorgente](#build-da-sorgente)
6. [Architettura](#architettura)
7. [Versioning](#versioning)
8. [Troubleshooting](#troubleshooting)
9. [Crediti e licenza](#crediti-e-licenza)

---

## Cosa fa e cosa serve

- **Input:** un flusso **RTSP** (telecamera IP, oppure un server come [MediaMTX](https://github.com/bluenviron/mediamtx) che ri-pubblica una webcam/USB — vedi sotto).
- **Output:** una webcam virtuale chiamata *"RTSP Virtual Camera"* visibile in tutte le app.
- **Requisiti:** Windows 11 21H2+. Per buildare da sorgente: Visual Studio 2022 (workload C++ e .NET desktop) + WiX Toolset (per l'MSI).

---

## Installazione (MSI)

Per il solo utilizzo, usa l'installer: **`RT-VirtualCam-Setup.msi`** (file unico, ~8 MB).

1. Esegui l'MSI e accetta la licenza MIT.
2. (Opzionale) spunta il collegamento sul desktop.
3. L'installer registra automaticamente il componente COM (nessun `regsvr32` manuale).

L'app si installa in `C:\Program Files\RTVirtualCamera`. Impostazioni e log finiscono in `%LOCALAPPDATA%\RTVirtualCamera` (l'install dir non è scrivibile da utente standard).

Per disinstallare: *App e funzionalità* di Windows, oppure ri-esegui l'MSI.

---

## Configurare la sorgente RTSP

Se la tua telecamera IP espone già RTSP, ti basta il suo URL (es. `rtsp://192.168.1.10:554/stream`). Se invece vuoi trasformare una **webcam USB** (es. su un mini-PC Linux/Raspberry) in una sorgente RTSP, lo schema collaudato è **MediaMTX + FFmpeg** via Docker.

> ⚠️ **Il punto più importante di tutta la configurazione.** Il source RTSP di Media Foundation è schizzinoso su come l'H.264 è "impacchettato". Se l'encoder produce **più slice per frame** (è il default di `-tune zerolatency`/`ultrafast` con thread multipli), MF tratta ogni slice come un frame a sé: ricevi 4–8× i frame reali, i timestamp si congelano e la latenza esplode. `ffplay` invece riassembla tutto e sembra a posto — quindi *non* fidarti solo di ffplay. **Forza un solo slice per frame** con `-x264-params sliced-threads=0`.

### docker-compose

```yaml
services:
  rtsp-server:
    image: bluenviron/mediamtx
    restart: unless-stopped
    ports:
      - "8554:8554"   # RTSP
      - "1935:1935"   # RTMP (opzionale)
      - "8888:8888"   # HLS (opzionale)
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

L'URL da mettere nell'app sarà `rtsp://<IP-del-server>:8554/webcam`.

### Regole d'oro per la bassa latenza

- **`-x264-params sliced-threads=0`** — un solo slice per frame (vedi avviso sopra). Imprescindibile con MF.
- **Un solo framerate, onesto.** Niente upsampling `-vf fps=N` da una cattura a framerate diverso: genera timestamp sintetici che MF interpreta male. Cattura e trasmetti allo stesso rate reale (se la webcam fa solo 15 fps, trasmetti 15).
- **`-tune zerolatency` + `-profile:v baseline`** — niente B-frame né lookahead.
- **`-g 30`** — keyframe ogni secondo (recupero rapido alla connessione).
- **`MTX_PROTOCOLS=tcp`** su LAN è affidabile. Su reti con perdite valuta UDP.

### Verifica

Sul server, prima di passare a Windows:

```bash
ffplay -fflags nobuffer -flags low_delay -framedrop -rtsp_transport tcp \
       rtsp://localhost:8554/webcam
```

Deve essere praticamente in tempo reale e riportare il framerate atteso (es. `30 fps`).

---

## Usare l'applicazione

1. Avvia **RTVirtualCamera.exe**.
2. Inserisci l'URL RTSP e premi **Start Preview** per vederlo nel pannello.
3. Premi **Start VCam**: comparirà *"RTSP Virtual Camera"* in Zoom/Teams/ecc.

### La barra di diagnostica (in alto)

Durante il preview, una barra mostra le metriche **del solo preview** (la camera per Zoom gira nel Frame Server, processo separato non leggibile da qui):

| Campo | Significato |
|---|---|
| **RX** | frame/s realmente ricevuti dalla sorgente (il valore *vero*, non quello nominale) |
| **render** | frame/s effettivamente disegnati |
| **drop** | frame/s scartati dal controllo adattivo (per restare in tempo reale) |
| **drift** | scostamento wall‑clock vs timeline media: ~stabile = latenza fissa, in crescita = accumulo |
| **copy** | costo dell'ultima copia frame (ms) |

Indicazione rapida: se **RX ≫ framerate reale** (es. 150 con sorgente a 30), quasi sicuramente stai trasmettendo H.264 multi‑slice → applica `sliced-threads=0` (vedi sopra).

---

## Build da sorgente

### Prerequisiti
- Visual Studio 2022 (C++ desktop + .NET desktop)
- WiX Toolset (per il progetto `Setup`)
- Windows 11 21H2+

### Compilare
Apri `RTVirtualCamera.sln` e compila in **x64**. I tre binari finiscono nella stessa cartella:

| Binario | Progetto |
|---|---|
| `VirtualCamera.dll` | `VirtualCamera/` (COM source, gira nel Frame Server) |
| `MFPipeline.dll` | `MFPipeline/` (bridge C++↔C#, preview) |
| `RTVirtualCamera.exe` | `RTVirtualCamera/` (UI WinForms) |

`Setup/` produce `RT-VirtualCam-Setup.msi`.

### Registrare la COM DLL (solo per sviluppo)

Dopo ogni rebuild, registra `VirtualCamera.dll`. Lo script si auto-eleva, ferma il Frame Server, ri-registra e riavvia:

```powershell
.\VirtualCamera\deploy_vcam.ps1 -DllPath ".\bin\x64\Debug\VirtualCamera.dll"
```

Per ripulire tra un test e l'altro: `.\VirtualCamera\unregister_vcam.ps1`.

> **La DLL deve stare in una cartella accessibile a tutti** (non sotto `C:\Users\...`), perché il Frame Server gira come *Local Service*. Una cartella sotto `C:\Projects\` va bene; il profilo utente causa `E_ACCESSDENIED` su `IMFVirtualCamera::Start`. L'MSI, installando in `Program Files`, risolve questo da sé.

---

## Architettura

```
RTVirtualCamera.exe (C# WinForms)
  └─ P/Invoke → MFPipeline.dll (C++)
       ├─ VideoPlayer  → PREVIEW nell'UI (EVR). Decodifica software, nel processo dell'app.
       └─ VirtualCamera → MFCreateVirtualCamera() + attributi RTSP, poi Start()

Windows Frame Server (svchost.exe)  ← processo separato, Local Service
  └─ carica VirtualCamera.dll (COM, registrata in HKLM)
       └─ apre l'RTSP con IMFSourceReader (decodifica GPU/DXVA, copia zero-copy)
       └─ consegna i frame a Zoom/Teams/…
```

Due percorsi **indipendenti** che aprono entrambi l'RTSP per conto proprio (nessuna memoria condivisa tra processi):

- **Preview** (nell'app): decodifica software + EVR. Comodo ma più pesante; a framerate alti scarta frame per restare reattivo. È quello che misura la barra di diagnostica.
- **Frame Server** (per Zoom): decodifica hardware DXVA con copia GPU→GPU, tiene sempre solo l'ultimo frame. È quello che conta per le videochiamate.

Dettagli per chi mette mano al codice: vedi [`CLAUDE.md`](CLAUDE.md).

---

## Versioning

Le versioni di **tutti** i componenti e dell'MSI sono derivate da **git**, rigenerate ad ogni build da [`build/Set-GitVersion.ps1`](build/Set-GitVersion.ps1):

- con un tag `vX.Y.Z` (+N commit dopo) → `X.Y.Z` (MSI) / `X.Y.Z.N` (file);
- senza tag → `0.1.<numero-commit>`;
- la stringa informativa porta lo SHA breve e `-dirty`.

Per una release pulita basta taggare: `git tag v1.0.0` → il build successivo propaga `1.0.0` ovunque (exe, DLL, MSI). I file generati (`Version.g.*`) non sono versionati.

---

## Troubleshooting

**Video in ritardo / latenza che cresce.** Quasi sempre è la sorgente: H.264 multi‑slice (`sliced-threads=0` lo risolve) o upsampling di framerate. Controlla la barra: `RX` deve essere ≈ al framerate reale, il `drift` stabile. Verifica con `ffplay` lato server.

**La virtual camera non appare o mostra frame neri.** La DLL non è registrata, oppure l'URL RTSP non è raggiungibile dalla macchina Windows, oppure manca la sorgente (frame di fallback).

**`E_ACCESSDENIED` / "Access Denied" su Start.** La `VirtualCamera.dll` è sotto `C:\Users\...`: spostala in una cartella pubblica e ri-registra. Con l'MSI non succede.

**`LNK1104: cannot open VirtualCamera.dll` ricompilando.** Il Frame Server tiene la DLL aperta. Chiudi le app che usano la camera, oppure esegui `unregister_vcam.ps1` prima del rebuild.

**Log e impostazioni.** In `%LOCALAPPDATA%\RTVirtualCamera\` (`debug.log`, `settings.json`).

**Debug del Frame Server.** È `svchost.exe`: in Visual Studio *Attach to Process* → l'istanza che ospita *Windows Camera Frame Server*. Le trace usano `WINTRACE` (provider ETW, visibile con TraceSpy).

---

## Crediti e licenza

Parte da [**VCamSample** di Simon Mourier](https://github.com/smourier/VCamSample) per l'impianto Media Foundation — grazie a lui per il lavoro originale.

Licenza **MIT** (vedi [`LICENSE`](LICENSE)), con copyright a Simon Mourier (originale) e Andrea Greco (riscrittura).
