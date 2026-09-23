# Piano: correttezza e latenza del percorso Frame Server + FFmpeg

Origine: revisione del codice del 2026-09-23 (Frame Server `VirtualCamera/` e producer FFmpeg
`RTCamNative/`). Il piano è diviso in **step indipendenti, da eseguire uno alla volta, in ordine**,
ciascuno in una shell/sessione separata. Ogni step è autocontenuto: chi lo implementa deve leggere
`CLAUDE.md` + la sezione "Regole comuni" + **solo il proprio step**.

## Stato

| # | Step | Area | Stato |
|---|---|---|---|
| 1 | Guardia geometria nel writer | app (`RTCamNative`) | ☑ |
| 2 | Nessuna richiesta persa + controllo buffer flat | Frame Server | ☑ |
| 3 | Seqlock robusto + controllo tearing + reset header | Frame Server | ☐ |
| 4 | `Lock2DSize` in sola scrittura + overlay nello stesso lock | Frame Server | ☐ |
| 5 | Wire format v2: slot allineati + evento "frame pronto" | Shared + entrambi | ☐ |
| 6 | Consegna guidata dall'evento (timer solo fallback) | Frame Server | ☐ |
| 7 | Decodifica/scaling direttamente nello slot condiviso | app | ☐ |
| 8 | Thread del decoder SW + scaler più veloce | app | ☐ |
| 9 | Resync senza freeze (catch-up a due livelli) | app | ☐ |
| 10 | (Sperimentale) `max_delay` UDP e avvio più rapido | app | ☐ |

Dipendenze: 6 richiede 5; 7 richiede 1 (e conviene dopo 5, che tocca lo stesso writer).
Gli altri sono indipendenti, ma vanno eseguiti in ordine per evitare conflitti sugli stessi file
(`MediaStream.cpp` è toccato da 2, 3, 4, 6; `FrameChannelWriter.*` da 1, 5, 7).

---

## Regole comuni (valgono per ogni step)

- **Leggi `CLAUDE.md`** prima di iniziare: architettura, invarianti, file nativi vs `/clr`.
- **Solo lo step assegnato.** Niente refactoring extra, niente "già che c'ero". Se trovi un problema
  fuori scopo, annotalo nel resoconto finale.
- **Stile:** come il codice circostante (commenti in inglese, stessa densità, `wil`, `RETURN_IF_FAILED`,
  `WINTRACE` nel Frame Server, `DebugLog` nell'app).
- **Invarianti da non rompere:** la mappatura frame e gli oggetti `Global\` sono creati **solo** dal
  Frame Server (`FrameChannelReader`); i file libav restano nativi (`CompileAsManaged=false`, no PCH);
  se cambia il layout di `Shared/VCamFrameChannel.h` → incrementa `VCAM_FRAMES_STRUCT_VERSION`.
- **Build:** x64 Debug. Se MSBuild è disponibile:
  `msbuild RTVirtualCamera.sln /p:Configuration=Debug /p:Platform=x64 /m`; altrimenti chiedi
  all'utente di compilare in Visual Studio e riportare gli errori.
  Se lo step tocca `VirtualCamera/` va rieseguito `deploy_vcam.ps1` (vedi `CLAUDE.md`).
- **Verifica manuale** (non ci sono test automatici): avviare l'app con una camera RTSP reale,
  avviare la virtual camera, aprirla in *Fotocamera di Windows* o Teams/Zoom; per il Frame Server
  usare `RTVCAM_TRACE=1` (ETW) e per l'app `RTVCAM_LOG=1`. Riportare nel resoconto cosa è stato
  verificato e cosa no.
- **Misure di riferimento** (per gli step di latenza 4, 6, 7, 8, 9): prima e dopo annotare
  `lastCopyMs` e fps render/rx dalle statistiche live dell'app, e — se possibile — la latenza
  end-to-end inquadrando con la camera IP un cronometro a video e fotografando insieme
  cronometro e finestra Teams/Fotocamera.
- **Documentazione:** se lo step cambia un comportamento descritto in `CLAUDE.md`, aggiorna
  `CLAUDE.md` nello stesso step. Alla fine spunta lo step nella tabella "Stato" qui sopra.
- **Commit:** uno per step, con il messaggio suggerito (adattabile), solo se l'utente lo chiede.

---

## Step 1 — Guardia geometria nel writer

**Problema.** `FrameChannelWriter::WriteFrame` (`RTCamNative/FrameChannelWriter.cpp`) copia
`_header->width × _header->height` pixel dal buffer sorgente, ma il sink in
`RTCamNative/FfmpegExports.cpp` ignora `w/h` del frame. Il buffer sorgente (`nv12` in
`FfmpegRtspSource::DecodeLoop`) è grande `targetW × targetH`. Se il Frame Server ristampa l'header
con una geometria più grande (riattivazione con config diversa mentre l'app gira:
`FrameChannelReader::EnsureMapped` → `stampHeader()`), il writer **legge oltre il buffer** → crash
o spazzatura.

**Modifiche.**
1. `FrameChannelWriter::WriteFrame`: aggiungere i parametri `uint32_t width, uint32_t height` del
   frame sorgente. Se non coincidono con `_header->width/_header->height`, **non scrivere**
   (return, eventualmente `DebugLog` limitato — non a ogni frame: loggare solo al cambio di stato).
   Controllare anche `VCamFrameChannel_Nv12Bytes(width,height) <= _header->bytesPerSlot`.
2. Leggere `width/height/stride/slotCount/bytesPerSlot` dall'header **una volta sola** in variabili
   locali all'inizio (già in parte così) e usare solo quelle.
3. `FfmpegExports.cpp`: il sink passa `w, h` a `WriteFrame`.
4. Aggiornare il commento di classe in `FrameChannelWriter.h`.

**Verifica.** Build; virtual camera funzionante come prima. (Il caso di mismatch è difficile da
riprodurre a mano: basta verificare con una lettura del codice che il ramo di mismatch non copi.)

**Commit.** `Frame channel writer: skip frames whose geometry doesn't match the header`

---

## Step 2 — Nessuna richiesta persa + controllo buffer flat

File: `VirtualCamera/MediaStream.cpp`.

**Problema A.** In `MediaStream::DispatchSamples`, se `ProduceAndQueue` fallisce con un errore
diverso da `MF_E_SAMPLEALLOCATOR_EMPTY`, il token di richiesta viene scartato senza consegnare nulla.
Il Frame Server tiene poche richieste in volo: ogni token perso le riduce in modo permanente →
dopo qualche errore lo stream può bloccarsi.

**Modifiche A.**
- In `ProduceAndQueue`, separare le fasi: se la copia o il frame sintetico falliscono, consegnare
  comunque il sample (anche nero/sintetico) invece di uscire con errore. Gli errori che restano
  fatali sono quelli di `AllocateSample` (≠ EMPTY), `SetUnknown(MFSampleExtension_Token)` e
  `QueueEventParamUnk`.
- In `DispatchSamples`, se `ProduceAndQueue` fallisce comunque con errore fatale: accodare sul
  `_queue` un evento `MEError` con l'HRESULT (così il Frame Server lo vede e chiude/riapre in modo
  pulito) e loggare con `WINTRACE`. Non restare in silenzio.

**Problema B.** In `CopyNv12ToSample`, ramo senza `IMF2DBuffer`: `MFCopyImage` scrive
`width*height*3/2` byte anche se `cbDstMax` è più piccolo (il `min` è applicato solo a
`SetCurrentLength`) → overflow.

**Modifiche B.** Se `cbDstMax < cbExpected`: `Unlock()` e restituire
`MF_E_BUFFERTOOSMALL` (niente copia). Nel ramo 2D, se `dstPitch < (LONG)width` fare lo stesso.

**Verifica.** Build + deploy; la camera funziona come prima; con `RTVCAM_TRACE=1` nessun nuovo
errore nel tracing durante una sessione normale.

**Commit.** `MediaStream: never drop a request token; bound the flat-buffer copy`

---

## Step 3 — Seqlock robusto, controllo tearing, reset header

File: `VirtualCamera/FrameChannelReader.cpp/.h`, `VirtualCamera/MediaStream.cpp`.

**Problema A — barriere.** In `FrameChannelReader::AcquireLatest` i campi non-volatile
(`frameSeq`, `framesWritten`, `producerHeartbeatTickMs`, `width`…) possono in teoria essere
riordinati dal compilatore rispetto alle letture di `publishSeq`.

**Modifiche A.** Nel reader: `std::atomic_thread_fence(std::memory_order_acquire)` (oppure
`_ReadBarrier()`) **dopo** la lettura di `seq0` e **prima** della lettura di `seq1`. Nel writer
(`RTCamNative/FrameChannelWriter.cpp`) gli `InterlockedIncrement` sono già barriere complete: non
toccarli.

**Problema B — tearing.** Il reader copia i pixel dello slot fuori dal seqlock. Il writer scrive lo
slot `latest+1`, poi `latest+2`, poi di nuovo `latest` (3 slot). Se durante la copia `frameSeq`
avanza di **≥ slotCount-1 (=2)** rispetto al `fseq` letto, il writer può aver iniziato a riscrivere
proprio lo slot in copia → frame "strappato".

**Modifiche B.**
1. Aggiungere a `FrameChannelReader` un metodo tipo
   `bool IsSlotStillValid(uint64_t acquiredFrameSeq) const` che rilegge `_header->frameSeq`
   (con barriera acquire prima) e restituisce `false` se `current - acquired >= slotCount - 1`.
2. In `MediaStream::CopyFrameChannelFrame`, dopo `CopyNv12ToSample`: se non valido, ripetere
   **una sola volta** acquire+copia; se ancora non valido, tenere la copia (meglio un frame
   imperfetto che uno perso) e contare l'evento (`WINTRACE` + un contatore interno; **non** cambiare
   `VCamFrameServerStats`, sarebbe un cambio di ABI).

**Problema C — header.** `AcquireLatest` non controlla `structVersion`; e in `EnsureMapped`, se la
mappatura è già mappata e la geometria è identica, l'header non viene resettato → uno slot della
sessione precedente può apparire "fresco" fino a 2 s.

**Modifiche C.**
1. `AcquireLatest`: `return false` se `_header->structVersion != VCAM_FRAMES_STRUCT_VERSION`.
2. `EnsureMapped`: sul ramo "già mappato" chiamare **sempre** `stampHeader()` (azzera `latestSlot`,
   heartbeat, contatori), non solo al cambio di geometria.

**Verifica.** Build + deploy; camera normale; chiudendo e riaprendo la virtual camera non si vede
per un istante il frame della sessione precedente.

**Commit.** `Frame channel reader: acquire fences, post-copy tearing check, always restamp header`

---

## Step 4 — `Lock2DSize` in sola scrittura + overlay nello stesso lock

File: `VirtualCamera/MediaStream.cpp/.h`.

**Problema.** I sample dell'output allocator sono texture D3D11 (il Frame Server passa il device
manager, `SetD3DManager`). `IMF2DBuffer::Lock2D` blocca in lettura+scrittura: MF scarica prima la
texture su CPU (rilettura inutile, poi la sovrascriviamo tutta) e la ricarica all'unlock.
In più `DrawOverlayCounter` fa un **secondo** `Lock2D/Unlock2D` → un altro giro completo.

**Modifiche.**
1. In `CopyNv12ToSample`: provare prima `IMF2DBuffer2` →
   `Lock2DSize(MF2DBuffer_LockFlags_Write, &scan0, &pitch, &bufStart, &cbBuf)`; se l'interfaccia non
   c'è, fallback all'attuale `Lock2D`. Usare `cbBuf` per validare che
   `pitch * height * 3 / 2 <= cbBuf` (altrimenti `MF_E_BUFFERTOOSMALL`).
2. Rifattorizzare l'overlay: `DrawOverlayCounter` diventa una funzione che lavora su piani già
   bloccati (`BYTE* y, BYTE* uv, LONG pitch`), chiamata da `CopyNv12ToSample` **prima**
   dell'unlock quando `_overlayEnabled`. Passare il valore del contatore come parametro.
   Il percorso sintetico (`FrameGenerator`) non cambia.
3. Rimuovere il secondo lock in `ProduceAndQueue`.
4. Aggiornare in `CLAUDE.md` la sezione "Frame copy path".

**Verifica.** Build + deploy; con overlay attivo il contatore è visibile e corretto; confrontare
`lastCopyMs` prima/dopo (atteso: calo sensibile se il sample è una texture GPU).

**Commit.** `MediaStream: write-only Lock2DSize and draw the overlay under the copy lock`

---

## Step 5 — Wire format v2: slot allineati + evento "frame pronto"

File: `Shared/VCamFrameChannel.h`, `VirtualCamera/FrameChannelReader.cpp/.h`,
`RTCamNative/FrameChannelWriter.cpp/.h`, `CLAUDE.md`.
Questo step **prepara** l'infrastruttura; il Frame Server inizierà a usare l'evento nello step 6.

**Modifiche — layout.**
1. `VCAM_FRAMES_STRUCT_VERSION` → `2u`.
2. Gli slot partono a un offset allineato: introdurre `VCAM_FRAMES_HEADER_BYTES` = `4096`
   (header riservato a una pagina, con `static_assert(sizeof(VCamFrameChannelHeader) <= ...)`) e
   usarlo in `VCamFrameChannel_SlotPtr` e nel calcolo della dimensione della mappatura.
3. `bytesPerSlot` arrotondato per eccesso a multiplo di 4096 (helper
   `VCamFrameChannel_SlotBytes(w,h)`); il calcolo NV12 "vero" resta `VCamFrameChannel_Nv12Bytes`.
   Aggiornare `stampHeader` e il controllo aggiunto nello step 1.
4. Il campo `stride` resta `== width` (non cambiare lo stride in questo step).

**Modifiche — evento.**
1. Nome: `VCAM_FRAMES_EVENT_NAME` = `L"Global\\RTVCam_FrameReady_3CAD447D-F283-4AF4-A3B2-6F5363309F52"`
   in `Shared/VCamFrameChannel.h`.
2. **Creato dal Frame Server** in `FrameChannelReader::EnsureMapped` (subito dopo la mappatura):
   `CreateEventW(&sa, FALSE /*auto-reset*/, FALSE, VCAM_FRAMES_EVENT_NAME)` con DACL
   `D:(A;;GA;;;LS)(A;;0x00100002;;;IU)` (SYNCHRONIZE | EVENT_MODIFY_STATE per Interactive Users).
   Handle conservato nel singleton, esposto con `HANDLE FrameReadyEvent() const`, chiuso nel
   distruttore. Se la creazione fallisce: solo `WINTRACE`, tutto il resto deve funzionare come
   prima (l'evento è un'ottimizzazione).
3. **Aperto dall'app** in `FrameChannelWriter::EnsureOpen` con
   `OpenEventW(EVENT_MODIFY_STATE, FALSE, VCAM_FRAMES_EVENT_NAME)`; se fallisce, il writer continua
   senza evento (riprovare ad aprirlo al massimo una volta al secondo, non a ogni frame).
4. `WriteFrame`: `SetEvent` **dopo** il secondo `InterlockedIncrement` (pubblicazione completa).
5. `Close()` chiude anche l'handle dell'evento.
6. `CLAUDE.md`: aggiornare "Two shared-memory channels", "Shared/VCamFrameChannel.h", "Critical
   invariants" (l'evento è creato solo dal Frame Server, come la mappatura).

**Nota di deploy.** Cambia il wire format: app e DLL del Frame Server vanno aggiornate insieme.
Una sezione v1 ancora viva (Frame Server non riavviato) viene rifiutata dal writer grazie al
controllo di `structVersion`: riavviare il servizio Frame Server dopo il deploy.

**Verifica.** Build + deploy + riavvio del Frame Server; la camera funziona come prima (il timer
continua a guidare la consegna); con `RTVCAM_TRACE=1` il tracing mostra l'evento creato; con
`RTVCAM_LOG=1` il log dell'app mostra l'evento aperto.

**Commit.** `Frame channel v2: page-aligned slots and a Global frame-ready event`

---

## Step 6 — Consegna guidata dall'evento (timer solo fallback)

Prerequisito: step 5. File: `VirtualCamera/MediaStream.cpp/.h`, `CLAUDE.md`.

**Problema.** Oggi un timer threadpool a periodo fisso (`1000*den/num` ms, arrotondato: 33 ms =
30,3 fps) rende "dovuto" un frame, indipendentemente da quando il producer lo pubblica. Costo:
in media +½ periodo (~16 ms) di latenza, e un battimento tra cadenza della camera (25 / 29,97 fps)
e del timer → frame duplicati/saltati periodicamente (judder).

**Modifiche.**
1. In `MediaStream::Start`: se `FrameChannelReader::Instance().FrameReadyEvent()` è valido, creare
   un threadpool wait (`CreateThreadpoolWait`) e armarlo con `SetThreadpoolWait(wait, hEvent, nullptr)`.
   Callback: prende `_lock`, se `_state == RUNNING` imposta `_frameDue = true`, chiama
   `DispatchSamples()`, poi **ri-arma** il wait (un wait threadpool scatta una sola volta per
   armamento). Registrare il tick dell'ultimo evento (`_lastFrameEventTick`, `GetTickCount64`).
2. Il timer resta, ma diventa **fallback**: nel tick imposta `_frameDue = true` solo se non arriva
   un evento da più di ~1,5 periodi (producer fermo/heartbeat stantio → frame sintetico alla
   cadenza nominale, o evento non disponibile → comportamento attuale).
3. `Stop()` e `Shutdown()`: come per il timer, **fuori** da `_lock`:
   `SetThreadpoolWait(wait, nullptr, nullptr)` + `WaitForThreadpoolWaitCallbacks(wait, TRUE)`;
   in `Shutdown` anche `CloseThreadpoolWait`. Attenzione al ri-armo nella callback: non ri-armare
   se `_state != RUNNING` (evita che un wait ri-armato sopravviva allo Stop).
4. Il credito resta singolo (`bool _frameDue`): una raffica di frame dal producer non produce una
   raffica di sample.
5. `CLAUDE.md`: aggiornare la descrizione della cadenza in "Key classes / MediaStream" e nel diagramma.

**Verifica.** Build + deploy; con overlay attivo il contatore avanza senza "doppioni" regolari;
fps render ≈ fps rx nelle statistiche (prima render era fisso a ~30,3); a app chiusa compare il
frame sintetico a cadenza regolare; nessun deadlock chiudendo/riaprendo più volte la camera
nel consumer. Misura di latenza prima/dopo.

**Commit.** `MediaStream: deliver on the producer's frame-ready event; timer as fallback`

---

## Step 7 — Decodifica/scaling direttamente nello slot condiviso

Prerequisiti: step 1 (e 5). File: `RTCamNative/FrameChannelWriter.cpp/.h`,
`RTCamNative/FfmpegRtspSource.cpp/.h`, `RTCamNative/FfmpegExports.cpp`, `CLAUDE.md`.
**Il preview (`FfmpegPreviewPlayer`) deve continuare a funzionare invariato** con il sink attuale.

**Problema.** Per frame HW oggi ci sono 3 passaggi completi app-side:
texture → staging → `swFrame` → `sws_scale` (NV12→NV12 a pari dimensione = copia) → `nv12` →
`memcpy` → slot. A 1080p30 sono ~6 MB/frame evitabili (~180 MB/s) e 1–3 ms di latenza per frame.

**Modifiche — writer.** API a due fasi, in aggiunta a `WriteFrame` (che resta):
- `bool BeginWrite(uint32_t w, uint32_t h, uint8_t* data[2], int linesize[2])`: verifica geometria
  (come step 1), sceglie lo slot successivo, restituisce i puntatori Y/UV dello slot e lo stride.
- `void CommitWrite()`: pubblica sotto seqlock (+ `SetEvent` dello step 5).
- `void AbortWrite()`: non pubblica nulla.

**Modifiche — FfmpegRtspSource.** Un "target diretto" opzionale, alternativo al sink:
- Nuovo tipo `FrameTarget` in `FfmpegRtspSource.h` (niente tipi libav nell'header):
  `std::function<bool(uint32_t w, uint32_t h, uint8_t* data[2], int linesize[2])> acquire;`
  `std::function<void(bool publish)> release;`
- Nuovo overload di `Start(...)` che riceve un `FrameTarget` invece di un `FrameSink`.
- Nel `DecodeLoop`, se c'è un target:
  - **HW (`AV_PIX_FMT_D3D11`) con `sw_format` NV12 e dimensioni == target:** scaricare
    direttamente nello slot con `av_hwframe_transfer_data(dst, frame, 0)`, dove `dst` è un
    `AVFrame` con `format=NV12`, `width/height`, `data/linesize` = slot.
    **Attenzione:** se `dst->buf[0]` è nullo, `av_hwframe_transfer_data` alloca buffer propri
    invece di usare i nostri → impostare `dst->buf[0]` con `av_buffer_create(slotPtr, size,
    noopFree, nullptr, 0)`. Verificare questo comportamento sul sorgente libav 8.1 incluso da vcpkg
    (`libavutil/hwcontext.c`, `hwcontext_d3d11va.c`) prima di affidarcisi; se non regge, usare
    `swFrame` + copia diretta nello slot (`av_image_copy`) saltando comunque `sws_scale`.
  - **Tutti gli altri casi (SW, formato diverso, scaling):** `sws_scale` con destinazione = slot.
  - Su errore: `release(false)`; su successo `release(true)`.
- Il buffer `nv12` intermedio si alloca solo se si usa il percorso a sink (preview).
- `FfmpegExports.cpp`: il producer usa il target (acquire → `EnsureOpen` + `BeginWrite`,
  release → `CommitWrite`/`AbortWrite`).

**Verifica.** Build; virtual camera con decode HW e con decode SW forzato (impostazioni): immagine
corretta, colori corretti, nessuna banda/strappo; il preview funziona ancora; RAM di processo
leggermente inferiore; misura di latenza prima/dopo.

**Commit.** `FFmpeg producer: decode/scale straight into the shared-memory slot`

---

## Step 8 — Thread del decoder SW + scaler più veloce

File: `RTCamNative/FfmpegRtspSource.cpp`.

**Problema.** `cc->thread_type = FF_THREAD_SLICE` è impostato, ma `thread_count` è lasciato al
default di libavcodec (**1**) → nessun parallelismo in decode software. Lo slice threading non
aggiunge latenza (a differenza del frame threading).

**Modifiche.**
1. Prima di `avcodec_open2`: `cc->thread_count = 0;` (auto), mantenendo `FF_THREAD_SLICE`.
   Commento: aiuta solo con stream multi-slice; irrilevante per il decode HW.
2. `sws_getCachedContext`: `SWS_FAST_BILINEAR` al posto di `SWS_BILINEAR` (conta solo quando si
   scala davvero). Valutare a occhio la qualità in un caso con scaling; se peggiora in modo visibile,
   tornare indietro e annotarlo.

**Verifica.** Decode SW forzato: CPU e latenza prima/dopo (Task Manager + statistiche); nessuna
regressione in HW.

**Commit.** `FFmpeg: enable slice threads for software decode; fast bilinear scaler`

---

## Step 9 — Resync senza freeze (catch-up a due livelli)

File: `RTCamNative/FfmpegRtspSource.cpp` (+ eventuale nuova impostazione se serve, vedi sotto).

**Problema.** Quando un frame supera il cap di ritardo (`MaxLagMs`, 350 ms), il loop scarta tutto
fino al keyframe successivo e fa il flush del decoder. Con GOP di 2–4 s l'immagine resta **ferma**
per quel tempo.

**Modifiche.**
1. **Livello 1 (catch-up):** se `lagMs > MaxLagMs`, entrare in modalità catch-up: continuare a
   decodificare ma **non presentare** i frame (niente `sws_scale`/copia/sink) finché
   `lagMs <= MaxLagMs / 2` (isteresi). Durante il catch-up impostare
   `cc->skip_frame = AVDISCARD_NONREF` (salta i frame non di riferimento), e rimetterlo a
   `AVDISCARD_DEFAULT` all'uscita.
2. **Livello 2 (fallback attuale):** se il catch-up dura più di ~1 s (o il ritardo continua a
   crescere), passare al comportamento attuale (skip al keyframe + `avcodec_flush_buffers` +
   ri-ancoraggio del clock).
3. Mantenere `_lastLagMs` aggiornato in entrambe le modalità; `DebugLog` all'ingresso/uscita di ogni
   modalità (non a ogni frame).
4. Il frame-stall detector (`lastFrameTick`) deve considerare "vivo" anche un frame decodificato ma
   non presentato, altrimenti il catch-up provoca riconnessioni spurie.
5. Aggiornare in `CLAUDE.md` la sezione "Latency handling".

**Verifica.** Provocare ritardo (es. decode SW forzato su stream 4K, o pausa di rete di 1–2 s con
Clumsy/filtro firewall): l'immagine riprende a scorrere fluida invece di congelarsi fino al keyframe;
la latenza torna sotto il cap.

**Commit.** `FFmpeg: catch up by skipping presentation before falling back to keyframe resync`

---

## Step 10 — (Sperimentale) `max_delay` UDP e avvio più rapido

File: `RTCamNative/FfmpegRtspSource.cpp`. **Da fare solo con misure sul campo;** se non danno
beneficio, non fare commit e annotare il risultato qui.

1. **`max_delay` su UDP.** Con `max_delay=0`, alla prima lacuna la coda di riordino RTP viene
   svuotata subito (da verificare su `libavformat/rtsp.c` di libav 8.1): i pacchetti fuori ordine
   vanno persi nonostante `reorder_queue_size` alto → possibili bande verdi. Provare 20–50 ms solo
   su UDP e confrontare artefatti vs latenza. Se utile, cambiare solo il default (l'impostazione
   utente esiste già: `MaxDelayMs`).
2. **Avvio.** Se dopo `avformat_open_input` il `codecpar` ha già `width/height` e `extradata`
   (SDP completo), saltare `avformat_find_stream_info` per accorciare la connessione iniziale e le
   riconnessioni. Tenerlo come fallback quando mancano i parametri.

**Verifica.** Tempo dalla connessione al primo frame (log con `RTVCAM_LOG=1`) prima/dopo; artefatti
su UDP con rete disturbata.

**Commit.** `FFmpeg: <quanto effettivamente adottato>`
