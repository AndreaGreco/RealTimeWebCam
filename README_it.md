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

<p align="center">
  <video src="https://github.com/user-attachments/assets/a57b0740-eda5-4500-aeb1-e71f141942c0"
         controls autoplay loop muted playsinline width="720"></video>
</p>

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

Se la tua telecamera parla già RTSP — la maggior parte delle telecamere IP e degli NVR lo fa — ti basta questo: trova l'URL nel manuale o nella web UI della telecamera (es. `rtsp://192.168.1.10:554/stream`) e incollalo nell'app. Tutto qui, passa a [Usare l'applicazione](#usare-lapplicazione).

> 💡 **Consiglio di tuning.** Se la web UI della telecamera permette di configurare l'encoder video, alcune impostazioni fanno una vera differenza per un'immagine fluida e a bassa latenza: attiva la codifica **zero-latency / low-delay**, usa un **profilo baseline** (niente B-frame), tieni corto il **GOP / intervallo dei keyframe** (circa 1 secondo) e trasmetti al framerate reale della telecamera, non a uno "gonfiato" via upsampling. Sul lato ricezione, le **Impostazioni** dell'app permettono di regolare il trasporto RTSP (Auto/UDP/TCP), la decodifica hardware e il cap di latenza — forza **TCP** se il pannello di connessione mostra che la sorgente perde frame.

Non hai una telecamera RTSP e vuoi usare una semplice webcam USB? È un setup separato, più avanzato — vedi [**Creare una sorgente RTSP da una webcam USB**](DEVELOPMENT.md#create-an-rtsp-source-from-a-usb-webcam) nella guida per sviluppatori.

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
