# RealTimeWebCam — da RTSP a webcam virtuale (Windows 11)

Accade in alcune organizzazioni di avere a disposizione una camera IP da portare sulle più comuni applicazioni di meeting: Zoom, Teams, Meet, Skype & co. Queste, per una limitazione tecnica, **non** accettano sorgenti RTSP dirette. Questo progetto registra una **webcam virtuale** che mostra i frame in arrivo dalla camera IP RTSP, così qualunque app che sappia scegliere una webcam può usarla.

Funziona su **Windows 11** (21H2 o successivo) grazie all'API [`MFCreateVirtualCamera`](https://learn.microsoft.com/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera).

> 🇬🇧 English version: **[README.md](README.md)** — 🛠️ Per compilare dai sorgenti o capire come funziona dentro: **[DEVELOPMENT.md](DEVELOPMENT.md)**.

---

## Indice

1. [Cosa fa e cosa serve](#cosa-fa-e-cosa-serve)
2. [Installazione (MSI)](#installazione-msi)
3. [Configurare la sorgente RTSP](#configurare-la-sorgente-rtsp) ← la parte che conviene leggere
4. [Usare l'applicazione](#usare-lapplicazione)
5. [Troubleshooting](#troubleshooting)
6. [Crediti e licenza](#crediti-e-licenza)

---

## Cosa fa e cosa serve

- **Input:** un flusso **RTSP** (telecamera IP, oppure un media server).
- **Output:** una webcam virtuale chiamata *"RTSP Virtual Camera"* visibile in tutte le app.
- **Requisiti:** Windows 11 21H2+.

---

## Installazione (MSI)

Per il solo utilizzo, usa l'installer: **`RT-VirtualCam-Setup.msi`** (file unico, self-contained — include il runtime .NET 10, quindi non serve installare nient'altro prima).

1. Esegui l'MSI e accetta la licenza MIT.
2. (Opzionale) spunta il collegamento sul desktop; il collegamento nel menu Start viene aggiunto automaticamente.
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

### Impostazioni

Apri **Impostazioni** dall'app per scegliere la lingua dell'interfaccia (Sistema / Italiano / English / Español / Deutsch) e per attivare l'avvio automatico (apre lo stream da solo al lancio, senza bisogno di click). Il cambio lingua ha effetto dopo il riavvio dell'app.

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

## Troubleshooting

**Video in ritardo / latenza che cresce.** Quasi sempre è la sorgente: H.264 multi‑slice (`sliced-threads=0` lo risolve) o upsampling di framerate. Controlla la barra: `RX` deve essere ≈ al framerate reale, il `drift` stabile. Verifica con `ffplay` lato server.

**La virtual camera non appare o mostra frame neri.** L'URL RTSP non è raggiungibile dalla macchina Windows, oppure manca ancora la sorgente (vedi il frame di fallback). Con un URL raggiungibile l'immagine compare in un secondo o due.

**`E_ACCESSDENIED` / "Access Denied" all'avvio.** Con l'installazione via MSI non succede — riguarda solo le DLL compilate/spostate a mano. Vedi [DEVELOPMENT.md](DEVELOPMENT.md).

**Log e impostazioni.** In `%LOCALAPPDATA%\RTVirtualCamera\` (`debug.log`, `settings.json`).

---

## Crediti e licenza

Parte da [**VCamSample** di Simon Mourier](https://github.com/smourier/VCamSample) per l'impianto Media Foundation — grazie a lui per il lavoro originale.

Licenza **MIT** (vedi [`LICENSE`](LICENSE)), con copyright a Simon Mourier (originale) e Andrea Greco (riscrittura).
