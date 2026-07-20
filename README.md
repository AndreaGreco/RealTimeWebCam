<div align="center">

# RealTimeWebCam

**Turn any RTSP / IP camera into a virtual webcam for Zoom, Teams, Meet and Skype on Windows 11.**

[![Build](https://github.com/AndreaGreco/RealTimeWebCam/actions/workflows/msbuild.yml/badge.svg)](https://github.com/AndreaGreco/RealTimeWebCam/actions/workflows/msbuild.yml)
[![Latest release](https://img.shields.io/github/v/release/AndreaGreco/RealTimeWebCam?label=release)](https://github.com/AndreaGreco/RealTimeWebCam/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/AndreaGreco/RealTimeWebCam/total)](https://github.com/AndreaGreco/RealTimeWebCam/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Windows 11](https://img.shields.io/badge/Windows-11%2021H2%2B-0078D6?logo=windows&logoColor=white)](#requirements)

### ⬇️ [**Download the installer**](https://github.com/AndreaGreco/RealTimeWebCam/releases/latest) &nbsp;·&nbsp; 🇮🇹 [Italiano](README_it.md) &nbsp;·&nbsp; 🛠️ [Building from source](DEVELOPMENT.md)

</div>

<!-- TODO screenshot: add docs/images/screenshot-main.png (main window with live preview
     and the diagnostics panels) and docs/images/demo.gif (app → RTSP connect → camera
     picked in Zoom), then replace this comment with:

<p align="center">
  <img src="docs/images/demo.gif" alt="RealTimeWebCam in action" width="720">
</p>
-->

---

Meeting apps — Zoom, Teams, Meet, Skype and friends — **can't open an RTSP source directly**; they only enumerate webcams. RealTimeWebCam bridges the gap: it registers a real **virtual webcam** fed by your RTSP IP camera, so any app that can pick a webcam can use the stream.

Free and open source, single self-contained MSI, no account and no cloud.

---

## Contents

1. [What it does](#what-it-does)
2. [Install (MSI)](#install-msi)
3. [Configure the RTSP source](#configure-the-rtsp-source) ← the part worth reading
4. [Use the app](#use-the-app)
5. [Troubleshooting](#troubleshooting)
6. [Roadmap](#roadmap)
7. [Credits and license](#credits-and-license)

---

## What it does

- **Input:** an **RTSP** stream — an IP camera, an NVR, or a media server such as [MediaMTX](https://github.com/bluenviron/mediamtx).
- **Output:** a virtual webcam named *"RTSP Virtual Camera"*, visible in every app that lists webcams.
- **Low latency by design:** FFmpeg decoding with a latency cap that resyncs to live instead of drifting.
- **Hardware accelerated:** GPU decoding via `d3d11va` when available, automatic software fallback.
- **Resilient networking:** UDP with automatic TCP fallback, or force either one; automatic reconnect on stream loss.
- **Live diagnostics:** real receive/render framerates, active transport, codec, bitrate and drift shown in-app.
- **Localized:** English, Italiano, Español, Deutsch.

### Requirements

**Windows 11** (21H2 or later) — the virtual camera is built on the [`MFCreateVirtualCamera`](https://learn.microsoft.com/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera) API, which does not exist on Windows 10.

---

## Install (MSI)

### ⬇️ [**Download `RT-VirtualCam-Setup.msi`**](https://github.com/AndreaGreco/RealTimeWebCam/releases/latest)

A single self-contained file — it bundles the .NET 10 runtime, so there's nothing else to install first.

1. Run the MSI and accept the MIT license.
2. (Optional) tick the desktop shortcut; a Start Menu shortcut is added automatically.
3. The installer registers the COM component automatically (no manual `regsvr32`).

The app installs to `C:\Program Files\RTVirtualCamera`. Settings and logs go to `%LOCALAPPDATA%\RTVirtualCamera` (the install folder isn't writable by a standard user).

To uninstall: Windows *Apps & features*, or run the MSI again.

---

## Configure the RTSP source

If your IP camera already exposes RTSP, all you need is its URL (e.g. `rtsp://192.168.1.10:554/stream`). If instead you want to turn a **USB webcam** (e.g. on a Linux mini-PC / Raspberry Pi) into an RTSP source, the proven recipe is **MediaMTX + FFmpeg** via Docker.

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

## Use the app

1. Launch **RTVirtualCamera.exe**.
2. Enter the RTSP URL and press **Start Preview** to see it in the panel.
3. Press **Start VCam**: *"RTSP Virtual Camera"* will appear in Zoom/Teams/etc.

### Settings

Open **Settings** from the app to:

- pick the interface language (System / Italiano / English / Español / Deutsch — takes effect after restarting the app);
- turn on **auto-start** (opens the stream automatically on launch, no click needed);
- choose the **RTSP transport** (Auto with TCP fallback / UDP only / TCP only) and fine-tune the FFmpeg engine (hardware decode on/off, socket timeout, RTP reorder-buffer depth, latency cap);
- toggle a diagnostic **frame-counter overlay** burned into the video (off by default).

### The diagnostics panels (at the top)

Two small tables sit above the video. **Connection** describes the open stream — container, **transport** (the one *actually* carrying frames, `UDP`/`TCP`), codec, pixel format, resolution, frame rate, bitrate. **Stats** shows live rates: while you're only previewing they refer to the preview; once the virtual camera is running they come from the Frame Server (the separate process that feeds Zoom).

| Field | Meaning |
|---|---|
| **State** | preview active / camera active / waiting for the source |
| **Engine** | `Preview`, or `FFmpeg HW` / `FFmpeg SW` — whether the decoder ran on the GPU (d3d11va) or in software |
| **Decode** | `GPU (d3d11va)` or `CPU (software)` |
| **RX (fps)** | frames/s actually received from the source (the *real* value, not the nominal one) |
| **Render (fps)** | frames/s actually delivered / drawn |
| **Duplicates (fps)** | frames re-served because the consumer polls faster than the source produces new ones (harmless) |
| **Dropped (fps)** | frames discarded to stay real-time (latency-cap resync) |
| **Processing (ms)** | cost of the last frame copy/render |
| **Drift (ms)** | wall-clock vs media timeline offset: ~stable = fixed latency, steadily rising = latency building up |

Quick tell: if **RX** sits well below the source's real framerate, the network or the source is dropping frames — try forcing **TCP** transport in *Settings → Network*.

---

## Troubleshooting

**Video lagging / latency that keeps growing.** The FFmpeg engine caps latency and resyncs to live, so this is rare; when it happens it's usually the source (framerate upsampling) or an unstable network. Check the stats: `RX` should be ≈ the real framerate and `Drift` stable. The two knobs to try are the latency cap and the transport (*Settings → Network*). Verify the source with `ffplay` on the server side.

**The virtual camera doesn't appear, or shows black frames.** Either the RTSP URL isn't reachable from the Windows machine, or there's no source yet (you'll see the fallback frame). With a reachable URL the picture appears within a second or two.

**`E_ACCESSDENIED` / "Access Denied" when starting.** This does not happen with the MSI install — it only affects hand-built/relocated DLLs. See [DEVELOPMENT.md](DEVELOPMENT.md).

**Logs and settings.** Under `%LOCALAPPDATA%\RTVirtualCamera\` (`debug.log`, `settings.json`).

---

## Roadmap

Planned, not implemented yet:

- [ ] **Multiple RTSP sources** — configure several streams and switch between them from the app.
- [ ] **Install without administrator rights** — a per-user install that needs no elevation.

Ideas, bug reports and camera-compatibility notes are welcome — open an [issue](https://github.com/AndreaGreco/RealTimeWebCam/issues).

---

## Credits and license

Builds on [**VCamSample** by Simon Mourier](https://github.com/smourier/VCamSample) for the Media Foundation scaffolding — thanks to him for the original work.

**MIT** license (see [`LICENSE`](LICENSE)), with copyright to Simon Mourier (original) and Andrea Greco (rewrite).
