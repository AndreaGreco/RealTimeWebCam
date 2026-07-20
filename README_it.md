<div align="center">

# RealTimeWebCam

**Usa qualsiasi telecamera IP / RTSP come webcam su Zoom, Teams, Meet e Skype in Windows 11.**

[![Build](https://github.com/AndreaGreco/RealTimeWebCam/actions/workflows/msbuild.yml/badge.svg)](https://github.com/AndreaGreco/RealTimeWebCam/actions/workflows/msbuild.yml)
[![Ultima release](https://img.shields.io/github/v/release/AndreaGreco/RealTimeWebCam?label=release)](https://github.com/AndreaGreco/RealTimeWebCam/releases/latest)
[![Download](https://img.shields.io/github/downloads/AndreaGreco/RealTimeWebCam/total)](https://github.com/AndreaGreco/RealTimeWebCam/releases)
[![Licenza: MIT](https://img.shields.io/badge/licenza-MIT-blue.svg)](LICENSE)
[![Windows 11](https://img.shields.io/badge/Windows-11%2021H2%2B-0078D6?logo=windows&logoColor=white)](#requisiti)

### ⬇️ [**Scarica l'installer**](https://github.com/AndreaGreco/RealTimeWebCam/releases/latest) &nbsp;·&nbsp; 🇬🇧 [English](README.md) &nbsp;·&nbsp; 🛠️ [Compilare dai sorgenti](DEVELOPMENT.md)

</div>

<!-- TODO screenshot: vedi la nota equivalente in README.md -->

---

Le app di meeting — Zoom, Teams, Meet, Skype & co. — per una limitazione tecnica **non** accettano sorgenti RTSP dirette: sanno elencare solo webcam. RealTimeWebCam colma il divario registrando una vera **webcam virtuale** alimentata dalla tua telecamera IP RTSP, così qualunque app che sappia scegliere una webcam può usare il flusso.

Gratuito e open source, un unico MSI self-contained, senza account e senza cloud.

---

## Indice

1. [Cosa fa e cosa serve](#cosa-fa-e-cosa-serve)
2. [Installazione (MSI)](#installazione-msi)
3. [Configurare la sorgente RTSP](#configurare-la-sorgente-rtsp) ← la parte che conviene leggere
4. [Usare l'applicazione](#usare-lapplicazione)
5. [Troubleshooting](#troubleshooting)
6. [Sviluppi futuri](#sviluppi-futuri)
7. [Crediti e licenza](#crediti-e-licenza)

---

## Cosa fa e cosa serve

- **Input:** un flusso **RTSP** — una telecamera IP, un NVR, oppure un media server come [MediaMTX](https://github.com/bluenviron/mediamtx).
- **Output:** una webcam virtuale chiamata *"RTSP Virtual Camera"*, visibile in tutte le app che elencano webcam.
- **Bassa latenza per costruzione:** decodifica FFmpeg con un tetto di latenza che si risincronizza al live invece di accumulare ritardo.
- **Accelerazione hardware:** decodifica su GPU via `d3d11va` quando disponibile, fallback software automatico.
- **Rete resiliente:** UDP con fallback automatico a TCP, oppure forzatura di uno dei due; riconnessione automatica in caso di caduta.
- **Diagnostica live:** framerate reali di ricezione/render, transport attivo, codec, bitrate e drift mostrati nell'app.
- **Localizzata:** Italiano, English, Español, Deutsch.

### Requisiti

**Windows 11** (21H2 o successivo) — la webcam virtuale si basa sull'API [`MFCreateVirtualCamera`](https://learn.microsoft.com/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera), che su Windows 10 non esiste.

---

## Installazione (MSI)

### ⬇️ [**Scarica `RT-VirtualCam-Setup.msi`**](https://github.com/AndreaGreco/RealTimeWebCam/releases/latest)

File unico, self-contained — include il runtime .NET 10, quindi non serve installare nient'altro prima.

1. Esegui l'MSI e accetta la licenza MIT.
2. (Opzionale) spunta il collegamento sul desktop; il collegamento nel menu Start viene aggiunto automaticamente.
3. L'installer registra automaticamente il componente COM (nessun `regsvr32` manuale).

L'app si installa in `C:\Program Files\RTVirtualCamera`. Impostazioni e log finiscono in `%LOCALAPPDATA%\RTVirtualCamera` (l'install dir non è scrivibile da utente standard).

Per disinstallare: *App e funzionalità* di Windows, oppure ri-esegui l'MSI.

---

## Configurare la sorgente RTSP

Se la tua telecamera IP espone già RTSP, ti basta il suo URL (es. `rtsp://192.168.1.10:554/stream`). Se invece vuoi trasformare una **webcam USB** (es. su un mini-PC Linux/Raspberry) in una sorgente RTSP, lo schema collaudato è **MediaMTX + FFmpeg** via Docker.

> 💡 **Codifica a bassa latenza alla sorgente.** L'app riceve e decodifica lo stream con **FFmpeg** nel proprio processo, quindi riassembla i frame in modo robusto — il vecchio vincolo di forzare un solo slice H.264 per frame **non serve più** (gli stream multi-slice ora si decodificano correttamente, esattamente come faceva da sempre `ffplay`). Ciò che conta ancora per un'immagine in tempo reale è codificare senza buffering: `-tune zerolatency`, un profilo baseline (niente B-frame) e un keyframe circa ogni secondo. La ricetta qui sotto fa già tutto questo.

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

- **`-tune zerolatency` + `-profile:v baseline`** — niente B-frame né lookahead.
- **Un solo framerate, onesto.** Niente upsampling `-vf fps=N` da una cattura a framerate diverso: inventa timestamp sintetici e aggiunge jitter. Cattura e trasmetti allo stesso rate reale (se la webcam fa solo 15 fps, trasmetti 15).
- **`-g 30`** — keyframe ogni secondo (recupero rapido alla connessione e dopo perdite di pacchetti).
- **`-x264-params sliced-threads=0`** — non più necessario (FFmpeg riassembla correttamente i frame multi-slice); si può lasciare, è innocuo.
- **Trasporto.** `MTX_PROTOCOLS=tcp` su LAN è solidissimo. L'app stessa usa di default **UDP con fallback automatico a TCP** e permette di forzare solo UDP o solo TCP in *Impostazioni → Rete* — il pannello di connessione mostra poi quale trasporto sta effettivamente trasportando i frame.

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

Apri **Impostazioni** dall'app per:

- scegliere la lingua dell'interfaccia (Sistema / Italiano / English / Español / Deutsch — ha effetto dopo il riavvio dell'app);
- attivare l'**avvio automatico** (apre lo stream da solo al lancio, senza bisogno di click);
- scegliere il **trasporto RTSP** (Auto con fallback TCP / solo UDP / solo TCP) e regolare finemente il motore FFmpeg (decodifica hardware on/off, timeout socket, profondità del buffer di riordino RTP, cap di latenza);
- attivare un **overlay diagnostico con contatore di frame** impresso sul video (disattivo di default).

### I pannelli di diagnostica (in alto)

Sopra il video ci sono due piccole tabelle. **Connessione** descrive lo stream aperto — contenitore, **trasporto** (quello che sta *davvero* trasportando i frame, `UDP`/`TCP`), codec, formato pixel, risoluzione, frame rate, bitrate. **Statistiche** mostra le velocità live: finché sei solo in preview si riferiscono al preview; quando la virtual camera è attiva arrivano dal Frame Server (il processo separato che alimenta Zoom).

| Campo | Significato |
|---|---|
| **Stato** | preview attivo / camera attiva / in attesa della sorgente |
| **Motore** | `Preview`, oppure `FFmpeg HW` / `FFmpeg SW` — se il decoder ha girato su GPU (d3d11va) o in software |
| **Decodifica** | `GPU (d3d11va)` oppure `CPU (software)` |
| **RX (fps)** | frame/s realmente ricevuti dalla sorgente (il valore *vero*, non quello nominale) |
| **Render (fps)** | frame/s effettivamente consegnati / disegnati |
| **Duplicati (fps)** | frame ri-serviti perché il consumatore interroga più in fretta di quanto la sorgente produca (innocuo) |
| **Persi (fps)** | frame scartati per restare in tempo reale (resync del cap di latenza) |
| **Elaborazione (ms)** | costo dell'ultima copia/render del frame |
| **Drift (ms)** | scostamento wall‑clock vs timeline media: ~stabile = latenza fissa, in crescita costante = accumulo |

Indicazione rapida: se **RX** è molto sotto il framerate reale della sorgente, la rete o la sorgente stanno perdendo frame — prova a forzare il trasporto **TCP** in *Impostazioni → Rete*.

---

## Troubleshooting

**Video in ritardo / latenza che cresce.** Il motore FFmpeg limita la latenza e si risincronizza sul live, quindi è raro; quando capita di solito è la sorgente (upsampling di framerate) o una rete instabile. Controlla le statistiche: `RX` deve essere ≈ al framerate reale e il `Drift` stabile. Le due manopole da provare sono il cap di latenza e il trasporto (*Impostazioni → Rete*). Verifica la sorgente con `ffplay` lato server.

**La virtual camera non appare o mostra frame neri.** L'URL RTSP non è raggiungibile dalla macchina Windows, oppure manca ancora la sorgente (vedi il frame di fallback). Con un URL raggiungibile l'immagine compare in un secondo o due.

**`E_ACCESSDENIED` / "Access Denied" all'avvio.** Con l'installazione via MSI non succede — riguarda solo le DLL compilate/spostate a mano. Vedi [DEVELOPMENT.md](DEVELOPMENT.md).

**Log e impostazioni.** In `%LOCALAPPDATA%\RTVirtualCamera\` (`debug.log`, `settings.json`).

---

## Sviluppi futuri

Pianificati, non ancora implementati:

- [ ] **Sorgenti RTSP multiple** — configurare più flussi e passare dall'uno all'altro dall'app.
- [ ] **Installazione senza diritti di amministratore** — installazione per-utente che non richiede elevazione.

Idee, segnalazioni di bug e note di compatibilità con le telecamere sono benvenute — apri una [issue](https://github.com/AndreaGreco/RealTimeWebCam/issues).

---

## Crediti e licenza

Parte da [**VCamSample** di Simon Mourier](https://github.com/smourier/VCamSample) per l'impianto Media Foundation — grazie a lui per il lavoro originale.

Licenza **MIT** (vedi [`LICENSE`](LICENSE)), con copyright a Simon Mourier (originale) e Andrea Greco (riscrittura).
