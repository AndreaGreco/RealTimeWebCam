# Third-party notices

## FFmpeg (LGPL v2.1+)

RTVirtualCamera's optional **FFmpeg receive engine** uses the FFmpeg libraries
(`libavcodec`, `libavformat`, `libavutil`, `libswscale`) to decode the RTSP stream
in user space. These libraries are used **unmodified** and linked **dynamically**
(separate DLLs shipped alongside the application), under the terms of the
**GNU Lesser General Public License, version 2.1 or later**.

- FFmpeg project: https://ffmpeg.org
- Source code: https://ffmpeg.org/download.html — the exact version is pinned in
  this repository's `vcpkg.json` (via the vcpkg baseline) and can be rebuilt with
  vcpkg. The build enables only LGPL-compatible components (no GPL-only features
  such as `libx264`/`libx265`).
- License text: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
  (also shipped as `ffmpeg-LICENSE.txt` in the vcpkg package's
  `share/ffmpeg/copyright`).

Because the FFmpeg libraries are shipped as separate DLLs, an end user may replace
them with a compatible build of their own, as required by the LGPL.

The Media Foundation receive engine (the default) does **not** use FFmpeg.
