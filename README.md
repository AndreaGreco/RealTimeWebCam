# RealTimeWebCam — turn an RTSP stream into a virtual webcam (Windows 11)

Some setups hand you an IP camera that you'd like to use in the usual meeting apps — Zoom, Teams, Meet, Skype and friends. For a technical limitation those apps **don't** accept RTSP sources directly. This project registers a **virtual webcam** that shows the frames coming from your RTSP IP camera, so any app that can pick a webcam can use it.

Works on **Windows 11** (21H2 or later) thanks to the [`MFCreateVirtualCamera`](https://learn.microsoft.com/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera) API.

> 🇮🇹 Versione italiana: **[README_it.md](README_it.md)** — 🛠️ Building from source or curious about the internals? See **[DEVELOPMENT.md](DEVELOPMENT.md)**.

---

## Contents

1. [What it does](#what-it-does)
2. [Install (MSI)](#install-msi)
3. [Configure the RTSP source](#configure-the-rtsp-source) ← the part worth reading
4. [Use the app](#use-the-app)
5. [Troubleshooting](#troubleshooting)
6. [Credits and license](#credits-and-license)

---

## What it does

- **Input:** an **RTSP** stream (IP camera, or a media server).
- **Output:** a virtual webcam named *"RTSP Virtual Camera"*, visible in every app.
- **Requirements:** Windows 11 21H2+.

---

## Install (MSI)

Download and run the installer: **`RT-VirtualCam-Setup.msi`** (single file, ~8 MB).

1. Run the MSI and accept the MIT license.
2. (Optional) tick the desktop shortcut.
3. The installer registers the COM component automatically (no manual `regsvr32`).

The app installs to `C:\Program Files\RTVirtualCamera`. Settings and logs go to `%LOCALAPPDATA%\RTVirtualCamera` (the install folder isn't writable by a standard user).

To uninstall: Windows *Apps & features*, or run the MSI again.

---

## Configure the RTSP source

If your IP camera already exposes RTSP, all you need is its URL (e.g. `rtsp://192.168.1.10:554/stream`). If instead you want to turn a **USB webcam** (e.g. on a Linux mini-PC / Raspberry Pi) into an RTSP source, the proven recipe is **MediaMTX + FFmpeg** via Docker.

> ⚠️ **The single most important thing in the whole setup.** Media Foundation's RTSP source is picky about how the H.264 is "packetized". If the encoder emits **multiple slices per frame** (the default for `-tune zerolatency`/`ultrafast` with multiple threads), MF treats every slice as a frame of its own: you receive 4–8× the real frames, timestamps freeze and latency explodes. `ffplay` re-assembles everything and looks fine — so *don't* trust ffplay alone. **Force a single slice per frame** with `-x264-params sliced-threads=0`.

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

- **`-x264-params sliced-threads=0`** — one slice per frame (see the warning above). Non-negotiable with MF.
- **One honest framerate.** No `-vf fps=N` upsampling from a capture running at a different rate: it generates synthetic timestamps that MF misreads. Capture and stream at the same real rate (if the webcam only does 15 fps, stream 15).
- **`-tune zerolatency` + `-profile:v baseline`** — no B-frames, no lookahead.
- **`-g 30`** — a keyframe every second (fast recovery on connect).
- **`MTX_PROTOCOLS=tcp`** is reliable on a LAN. On lossy networks consider UDP.

### Verify

On the server, before moving to Windows:

```bash
ffplay -fflags nobuffer -flags low_delay -framedrop -rtsp_transport tcp \
       rtsp://localhost:8554/webcam
```

It should be practically real-time and report the expected framerate (e.g. `30 fps`).

---

## Use the app

1. Launch **RTVirtualCamera.exe**.
2. Enter the RTSP URL and press **Start Preview** to see it in the panel.
3. Press **Start VCam**: *"RTSP Virtual Camera"* will appear in Zoom/Teams/etc.

### The diagnostics bar (at the top)

While previewing, a bar shows metrics for the **preview only** (the camera that Zoom sees runs inside the Frame Server, a separate process that can't be read from here):

| Field | Meaning |
|---|---|
| **RX** | frames/s actually received from the source (the *real* value, not the nominal one) |
| **render** | frames/s actually drawn |
| **drop** | frames/s discarded by the adaptive control (to stay real-time) |
| **drift** | wall-clock vs media timeline offset: ~stable = fixed latency, rising = build-up |
| **copy** | cost of the last frame copy (ms) |

Quick tell: if **RX ≫ the real framerate** (e.g. 150 with a 30 fps source), you're almost certainly streaming multi-slice H.264 → apply `sliced-threads=0` (see above).

---

## Troubleshooting

**Video lagging / latency that keeps growing.** Almost always the source: multi-slice H.264 (`sliced-threads=0` fixes it) or framerate upsampling. Check the bar: `RX` should be ≈ the real framerate and `drift` stable. Verify with `ffplay` on the server side.

**The virtual camera doesn't appear, or shows black frames.** Either the RTSP URL isn't reachable from the Windows machine, or there's no source yet (you'll see the fallback frame). With a reachable URL the picture appears within a second or two.

**`E_ACCESSDENIED` / "Access Denied" when starting.** This does not happen with the MSI install — it only affects hand-built/relocated DLLs. See [DEVELOPMENT.md](DEVELOPMENT.md).

**Logs and settings.** Under `%LOCALAPPDATA%\RTVirtualCamera\` (`debug.log`, `settings.json`).

---

## Credits and license

Builds on [**VCamSample** by Simon Mourier](https://github.com/smourier/VCamSample) for the Media Foundation scaffolding — thanks to him for the original work.

**MIT** license (see [`LICENSE`](LICENSE)), with copyright to Simon Mourier (original) and Andrea Greco (rewrite).
