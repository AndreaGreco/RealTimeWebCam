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

<p align="center">
  <video src="https://github.com/user-attachments/assets/a57b0740-eda5-4500-aeb1-e71f141942c0"
         controls autoplay loop muted playsinline width="720"></video>
</p>

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

If your camera already speaks RTSP — most IP cameras and NVRs do — that's all you need: find its URL in the camera's manual or web UI (e.g. `rtsp://192.168.1.10:554/stream`) and paste it into the app. That's it, move on to [Use the app](#use-the-app).

> 💡 **Tuning tip.** If your camera's web UI lets you configure the video encoder, a few settings make a real difference for a real-time, low-latency picture: enable **zero-latency / low-delay** encoding, use a **baseline profile** (no B-frames), keep the **GOP / keyframe interval** short (about 1 second), and stream at the camera's own honest framerate rather than an upsampled one. On the receiving side, the app's *Settings* let you fine-tune the RTSP transport (Auto/UDP/TCP), hardware decode and the latency cap — force **TCP** if the connection panel shows the source dropping frames.

Don't have an RTSP camera and want to use a plain USB webcam instead? That's a separate, more advanced setup — see [**Create an RTSP source from a USB webcam**](DEVELOPMENT.md#create-an-rtsp-source-from-a-usb-webcam) in the developer guide.

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
