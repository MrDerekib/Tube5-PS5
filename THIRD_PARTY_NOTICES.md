# Third-party notices

## Credits and acknowledgements

| Project | Role |
| --- | --- |
| [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) | Public PS5 headers, libc++ headers, sysroot, and Clang target support |
| [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo) | Optional prebuilt PS5 ports and static libraries |
| [SvenGDK/SharpProspero](https://github.com/SvenGDK/SharpProspero) | Source of the ELF converter and FSELF writer in `tooling/native/` (GPL-3.0) |
| [LLVM/Clang](https://github.com/llvm/llvm-project) | Native compiler |
| [GoogleTest](https://github.com/google/googletest) | Pinned host-only C++ unit-test framework |
| [zlib](https://zlib.net/) | Pinned source-built compression library used by the host FSELF tool |
| [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex) | `texconv` presentation-image preparation |
| [FFmpeg](https://ffmpeg.org/) | Developer-supplied selection-audio preparation |
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) | External, developer-supplied tool for the private PC-side YouTube pilot; not bundled or linked into the PS5 app |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | Directory-style deployment and hardware validation |
| [ItsBlurf/BFplayer](https://github.com/ItsBlurf/BFplayer) | Playback, audio-clock and presentation reference; its fork is not bundled |
| [mihawk-99/PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | Public direct-memory definitions and CPU-cache measurements used to select the SDL working framebuffer's memory type; no driver code bundled |
| [ArkSama/PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon) | Original Lapy project and owned-root design |
| [mpereiraesaa/PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon) | Exact-title one-shot helper and cooperative elevation protocol |

## Native build dependencies

### SponsorBlock API data (separate from code)

Tube5 uses data from [SponsorBlock](https://sponsor.ajay.app/), by Ajay
Ramachandran and community contributors, under
[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).
See the [API/database license](https://github.com/ajayyy/SponsorBlock/wiki/Database-and-API-License).
The pilot filters sponsor/skip marks, merges intervals and converts seconds
to milliseconds. These transformed data retain their data license, not the
GPL license of Tube5's independently written integration code. Source/license
are shown in-app and in LAN response headers; private cached records are not
committed or bundled. No endorsement by SponsorBlock is implied.


### Tube5 playback prototype

Tube5 01.000.001 statically links SDL2 2.30.12, SDL_kitchensink 2.0.0-a2,
FFmpeg 7.0.1 and libass 0.17.3 from the pinned PacBrew v0.40.2 ports image.
SDL2 uses the zlib license; SDL_kitchensink uses MIT (not LGPL).
FFmpeg's PacBrew recipe enables GPL and version 3 support and libx264;
this is **not** an LGPL-only FFmpeg build. Tube5 remains GPL-3.0-or-later.

The selected libraries pull in OpenSSL, x264, libass, FreeType, Fontconfig,
HarfBuzz, FriBidi, libiconv, libsamplerate, zlib, libpng, bzip2, liblzma and
Expat through their static pkg-config dependencies. Their individual upstream
licenses apply in addition to Tube5's license. Before a public binary release,
complete the license-text and corresponding-source bundle for these dependencies;
see `docs/ROADMAP.md`. This development prototype is not release-ready.

DIAG42 additionally statically links PacBrew libcurl 8.18.0 for the direct
YouTube HTTPS metadata probe, including OpenSSL 3.5.2, libpsl and zstd through
pkg-config dependencies. libcurl uses the curl license, OpenSSL 3 uses
Apache-2.0, libpsl uses MIT and zstd offers BSD-3-Clause/GPL-2.0 dual licensing.
Their license texts and corresponding source requirements must be included
in the release audit. The native HTTPS adapter reuses the template's
`examples/update-check/console_curl.c` (BlackBearReloaded, GPL-3.0-or-later),
preserving Tube5's existing fcntl/dladdr implementations and SDK mkstemp.

DIAG43 adds PacBrew json-c 0.19 (MIT) for strict, bounded player-response
parsing; its license/source must be included in the release audit. The
experimental Android InnerTube protocol profile is researched from
[yt-dlp's maintained client profiles](https://github.com/yt-dlp/yt-dlp/blob/master/yt_dlp/extractor/youtube/_base.py).
No yt-dlp implementation, Python runtime or JavaScript extractor is bundled
in the native app. Profile/version changes and YouTube restrictions remain
compatibility risks, not a promise of unrestricted playback.

Build recipes and source archive references are published in
[pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo).
The SDL_kitchensink source is pinned at
`c1bac5287e14122b2cf886628bdb802cf3737d83` (tag `2.0.0-a2`). Its video timing
adjustment is reproduced in `tools/patches/kitchensink-video-timing.patch`.
Tube5's independently implemented `src/audio_reader.cpp` also replaces its
partial PCM reader, using the pinned decoder/audio layout documented in
`src/audio_reader_abi.hpp`. BFplayer's owned-frame approach was an investigation
reference; its reader and audio-clock implementation are not bundled. The
upstream SDL_kitchensink library and its MIT license still apply.
The local clip in `assets/test-clip.mp4` is an original synthetic test signal,
generated by `tools/generate-test-clip.sh`; it contains no third-party footage
or music. FFmpeg and libx264 used to generate it are build tools, not part of
the clip's content license.

SDL_kitchensink — The MIT License (MIT)

Copyright (c) 2018 Tuomas Virtanen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

SDL2 — zlib license

Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The application does not redistribute or dynamically load the
complete libc++ or libc++abi archives.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PS5-Lapy-JB-Daemon integration

The sandbox-elevation build fetches
[mpereiraesaa's PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon)
at commit `54a095c0f19161825e845daa760a03b446e654fa`, invokes its unmodified
`owned-helper` target for the selected application's exact title and packages
the generated helper ELF with Lapy's MIT license. The shared protocol header
published upstream is LGPL-2.1-or-later; this repository's application-side
wire implementation is GPL-3.0-or-later.

Lapy was created by
[ArkSama / Team PHU](https://github.com/ArkSama/PS5-Lapy-JB-Daemon). Credit
belongs to ArkSama, mpereiraesaa and the Lapy contributors. No Lapy kernel
source is copied or modified here.

The helper build also uses the pinned `ps5log/1` header from
[mpereiraesaa/ps5-agc-gears](https://github.com/mpereiraesaa/ps5-agc-gears/tree/1ae1f9182abd2770c131b97419034fb85173c2dc/native/ps5log),
GPL-3.0-or-later, and the official PS5 Payload SDK v0.40. Those build inputs
remain under ignored `.deps/lapy/`; the normal application toolchain remains
the separately pinned Payload SDK v0.42.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

The update-check example (`make update-check-example`) selects `libcurl`, which
statically links these PacBrew libraries into the example title. Its build
output stays under ignored `dist/` and is not distributed by this repository; an
application that ships the update check ships them and must carry their notices:

| Component | Version in PacBrew v0.40.2 | License |
| --- | --- | --- |
| [libcurl](https://curl.se/) | 8.18.0 | curl license (MIT/X derivative) |
| [OpenSSL](https://www.openssl.org/) | 3.5.2 | Apache License 2.0 |
| [zlib](https://zlib.net/) | 1.3.2 | zlib license |
| [zstd](https://github.com/facebook/zstd) | 1.5.6 | BSD-3-Clause (dual-licensed with GPL-2.0) |
| [libpsl](https://github.com/rockdaboot/libpsl) | 0.21.5 | MIT; built-in Public Suffix List data MPL-2.0 |

`examples/update-check/console_curl.c`, which makes these libraries run in a
native title, is original BlackBearReloaded code (GPL-3.0-or-later), taken from
the ProsperoRadio and ProsperoLichess projects. Its `gmtime_r` follows Howard
Hinnant's public-domain `civil_from_days` algorithm.

## Vendored miniz

`third_party/miniz/` holds [miniz](https://github.com/richgel999/miniz) 3.0.2
(commit `293d4db1b7d0ffee9756d035b9ac6f7431ef8492`), MIT, unmodified, with its
`LICENSE`; `SOURCE.json` records each file's SHA-256. Only the self-update
helper (`examples/self-update-helper`) and its host test link it; an
application that ships that helper ships miniz and must carry its notice. The
helper's archive validation and file helpers are original BlackBearReloaded
code (GPL-3.0-or-later), taken from the ProsperoStore project.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Original presentation assets

The BlackBear icon, selection artwork, and default selection track
`sce_sys/snd0.at9` are original assets supplied by BlackBearReloaded, Copyright
(C) 2026 BlackBearReloaded, and distributed under GPL-3.0-or-later. The track
is titled `Night Drive`.

No proprietary runtime module, encryption key, or game file is included.
