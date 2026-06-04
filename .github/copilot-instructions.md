# RealTimeWebCam — Project Context

## Scopo
Prendere un flusso RTSP da una camera di rete e presentarlo come **virtual camera** di Windows, così che Zoom (che non gestisce RTSP autonomamente) la possa usare come se fosse una webcam.

## Ambiente di sviluppo
- Sviluppo su Linux via Samba (`/run/user/1000/gvfs/smb-share:server=devel.local,...`)
- Compilazione su **guest Windows con Visual Studio 2022**
- Soluzione: `RTVirtualCamera.sln`

---

## Progetti nella soluzione

### 1. `VirtualCamera/` → `VCamSampleSource.dll` (COM DLL)
- Implementa `IMFMediaSourceEx` per Windows Media Foundation
- Registrata in HKLM con `regsvr32`, caricata dal servizio **Frame Server** (svchost)
- CLSID: `{3CAD447D-F283-4AF4-A3B2-6F5363309F52}` (deve corrispondere ovunque)
- Basata su [VCamSample di Simon Mourier](https://github.com/smourier/VCamSample)
- **Classi principali:**
  - `MediaSource` — implementa `IMFMediaSourceEx`. In `Initialize()` legge l'attributo `MF_VCAM_RTSP_URL` e lo propaga agli stream. In `Start()` passa l'URL a `MediaStream::SetRTSPUrl()`. **Preferenza architetturale:** la configurazione della camera deve essere impostata in un solo punto chiaro; se il manager RTSP non ottiene davvero NV12/size attesi dal reader, il path va considerato failure; in stato di failure MediaStream deve produrre frame sintetici.
  - `MediaStream` — implementa `IMFMediaStream2`. In `RequestSample()` chiama `ReadRTSPFrame()` se `_rtspReader` è disponibile, altrimenti frame nero.
  - `FrameGenerator` — genera frame sintetici via Direct2D. **NON è più chiamato** in `RequestSample`; è codice morto (rimasto dall'esempio originale).

### 2. `MFPipeline/` → `MFPipeline.dll`
- DLL C++ caricata dalla C# app (non dal Frame Server)
- Esporta funzioni C-style per P/Invoke da C#
- **Classi principali:**
  - `VideoPlayer` — player MF con EVR sink per preview video in un pannello UI Windows. Usa `VideoReaderCall` (callback `IMFSourceReaderCallback`) per ricevere frame decodificati.
  - `VirtualCamera` — wrappa `IMFVirtualCamera`. Chiama `MFCreateVirtualCamera()` con il CLSID di VCamSampleSource. Ha `SetRTSPUrl()` che chiama `IMFVirtualCamera::AddProperty()` per comunicare l'URL al Frame Server.
  - `CSourceOpenMonitor` — usato solo da `VideoPlayer` per open asincrono.

### 3. `RTVirtualCamera/` → `RTVirtualCamera.exe` (C# WinForms, namespace `TestVideo`)
- App di controllo. Usa P/Invoke su `MFPipeline.dll`.
- `VideoPlayerWrapper` — preview RTSP/file nel pannello UI
- `VirtualCameraWrapper` — gestisce register/start/stop virtual camera
- Il costruttore `VirtualCameraWrapper(VideoPlayerWrapper video)` passa il native handle del VideoPlayer al C++ — questo coupling è **architetturalmente sbagliato** (cross-process) e va rimosso.

---

## Flusso corretto (come dovrebbe funzionare)

```
C# app (RTVirtualCamera.exe)
  └─ chiama MFPipeline.dll → MFCreateVirtualCamera(CLSID_VCam, ...)
       └─ imposta MF_VCAM_RTSP_URL via IMFVirtualCamera::AddProperty

Windows Frame Server (svchost.exe)
  └─ carica VCamSampleSource.dll
       └─ MediaSource::Initialize() legge MF_VCAM_RTSP_URL dagli attributes
       └─ MediaSource::Start() → MediaStream::SetRTSPUrl(url) → MediaStream::Start()
           └─ MediaStream::InitializeRTSPReader() → IMFSourceReader su RTSP URL
               └─ MediaStream::RequestSample() → ReadRTSPFrame() → frame a Zoom/Teams
```

**Nessuna shared memory necessaria.** Il Frame Server apre RTSP da solo nel suo processo.

---

## Bug noti da correggere

### BUG 1 — Guard invertita in `InitializeRTSPReader`
**File:** `VirtualCamera/MediaStream.cpp` ~riga 72
```cpp
// SBAGLIATO:
if (!_rtspUrl.empty()) { return S_FALSE; }
// CORRETTO:
if (_rtspUrl.empty()) { return S_FALSE; }
```
Risultato: il reader RTSP non viene mai inizializzato anche quando l'URL è impostato.

### BUG 2 — Guard invertita in `VirtualCamera::SetRTSPUrl`
**File:** `MFPipeline/VirtualCamera.cpp`
```cpp
// SBAGLIATO:
if (url.empty()) { _rtspUrl = url; ... }
// CORRETTO: rimuovere il check, sempre salvare e propagare
```
Risultato: `SetRTSPUrl("rtsp://...")` non fa nulla.

### BUG 3 — ~~Mismatch GUID/DEVPROPKEY per `MF_VCAM_RTSP_URL`~~ ✅ RISOLTO
- **Causa radice:** `IMFVirtualCamera` eredita da `IMFAttributes`. Il meccanismo corretto per passare dati al Frame Server è `_vcam->SetString(guid, valore)` PRIMA di `Start()`. `AddProperty` scrive device properties PnP nel registro — non sono accessibili via `IMFAttributes::GetString` in `Initialize()`.
- **Bug aggiuntivo:** `const GUID MF_VCAM_RTSP_URL;` + `DEFINE_GUID(...)` nello stesso file causa doppia definizione — GUID restava zero-inizializzato.
- **Fix applicato:**
  - `MFPipeline/VirtualCamera.cpp`: rimosso `DEFINE_DEVPROPKEY`, aggiunto `static const GUID MF_VCAM_RTSP_URL = {...}`, `AddProperty` → `_vcam->SetString(MF_VCAM_RTSP_URL, url)`
  - `VirtualCamera/MediaSource.cpp`: rimosso `const GUID MF_VCAM_RTSP_URL; + DEFINE_GUID(...)`, aggiunto `static const GUID MF_VCAM_RTSP_URL = {...}` (stesso valore)

### BUG 4 — Risoluzione fissa 1280×960 in `MediaStream`
Il descriptor dello stream è hardcoded. Se il RTSP ha una risoluzione diversa, i frame vengono copiati male o rigettati.

---

## Codice morto / da rimuovere o ignorare

| Componente | Stato | Note |
|---|---|---|
| `FrameGenerator` (VirtualCamera/) | Morto | Mai chiamato in `RequestSample`; era del sample originale |
| `VideoPlayer` (MFPipeline/) | Parzialmente utile | OK per preview in UI, NON coinvolto nel path virtual camera |
| `VideoReaderCall` / `CSourceOpenMonitor` | Usati solo da `VideoPlayer` | Rimangono se si vuole il preview |
| `VirtualCamera(VideoPlayer*)` coupling | Da eliminare | Non ha senso passare handle cross-process |
| `ComCamera/` | Probabilmente legacy | Solo binari compilati, nessun sorgente visibile |

---

## Stato attuale (maggio 2026)
- L'architettura corretta (RTSP letto dentro Frame Server) è **quasi implementata** ma bloccata dai bug 1, 2, 3.
- La virtual camera si avvia ma mostra frame neri (il reader RTSP non si inizializza mai).
- Non esiste ancora un campo UI in C# per inserire l'URL RTSP (usa solo file browser per file locali).
- Precedente tentativo abbandonato: aprire RTSP dal lato C# e passare i frame via shared memory → correttamente scartato.

---

## Prossimi passi suggeriti
1. Correggere BUG 1 (`InitializeRTSPReader` guard invertita)
2. Correggere BUG 2 (`SetRTSPUrl` guard invertita in MFPipeline)
3. Risolvere BUG 3 (mismatch GUID/DEVPROPKEY): analizzare come `AddProperty` espone l'attributo e allineare la lettura in `MediaSource::Initialize`
4. Aggiungere text box RTSP URL nell'UI C# e collegare a `VirtualCameraWrapper`
5. Adattare la risoluzione dello stream al sorgente RTSP (rimuovere hardcoding 1280×960)
6. Eliminare il `VideoPlayer*` dal costruttore di `VirtualCamera` in MFPipeline
7. Separare la logica del client RTSP in una classe dedicata/singleton condivisa, che deve essere aperta quando l'utente avvia la camera dall'app C#; deposita l'ultimo frame in memoria condivisa interna, e `MediaStream` legge quel frame, lo converte nel formato richiesto e lo consegna al consumer (es. Zoom).
8. Quando si modifica una struct condivisa tra C# e C++, aggiornare anche il lato speculare/codice che la usa; preferenza per modifiche minime senza cambiamenti superflui.
9. Evitare indagini sull'Output di Visual Studio e procedere direttamente sulle modifiche al codice.
