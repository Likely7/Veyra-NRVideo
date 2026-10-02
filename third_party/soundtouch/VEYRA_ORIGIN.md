# SoundTouch 2.4.1

Upstream: https://codeberg.org/soundtouch/soundtouch.git
Tag: 2.4.1
Commit: 0047e0b1ecfceb041348579119bf79b73a322a3a
Licence: LGPL-2.1; see COPYING.TXT and each source header.
Author: Olli Parviainen and upstream contributors.

Copied unchanged: include/*.h, source/SoundTouch/*.{h,cpp}, COPYING.TXT, README.html.
Veyra supplies CMakeLists.txt for a private static float-sample build, without the command-line utility or DLL wrapper. Veyra's file-preview audio owner uses the public SoundTouch API; original audio channel layout and media timestamps remain Veyra's responsibility.
