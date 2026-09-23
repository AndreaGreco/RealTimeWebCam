# Third-party notices

## FFmpeg (LGPL v2.1+)

RTVirtualCamera's receive engine uses the FFmpeg libraries (`libavcodec`,
`libavformat`, `libavutil`, `libswscale`) to decode the RTSP stream in user
space. They are linked **dynamically** (separate DLLs shipped alongside the
application), under the terms of the **GNU Lesser General Public License,
version 2.1 or later**. The version actually loaded is shown in the app's
About dialog.

- FFmpeg project: https://ffmpeg.org
- Version: **FFmpeg 8.1.2**, built from source with the vcpkg `ffmpeg` port
  (8.1.2, port-version 3), features `avcodec`, `avformat`, `swscale`.
- Source code: https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz. The exact build
  recipe is in this repository: `vcpkg.json` pins the vcpkg baseline
  (`af629d8717ee75a8c2418135064ffd59a7019f1c`), and the vcpkg submodule at
  `external/vcpkg` contains the port used at that baseline
  (`external/vcpkg/ports/ffmpeg`: build script, configure options and the
  port's own patches). Building the solution reproduces the shipped DLLs.
- The build enables only LGPL-compatible components (no `--enable-gpl`, no
  `--enable-nonfree`, no GPL-only libraries such as `libx264`/`libx265`).
- License text: installed next to this file as `ffmpeg-LICENSE.txt` (FFmpeg's
  own license file, from the vcpkg package's `share/ffmpeg/copyright`); also at
  https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

Because the FFmpeg libraries are shipped as separate DLLs, an end user may replace
them with a compatible build of their own, as required by the LGPL.
