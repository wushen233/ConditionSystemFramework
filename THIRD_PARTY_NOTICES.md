# Third-Party Notices

This repository contains the independently maintained native backend and UI
source for ConditionSystemFramework. It does not contain compiled SWF files,
FallUI media assets, plugin records, Papyrus binaries, or other compiled game
assets.

## UI integration

### FallUI compatibility patches

`ui/actionscript/fallui-patches` contains ActionScript menu files modified for
ConditionSystemFramework interoperability. The CSF additions are released as
source, but the underlying Fallout 4 and FallUI portions remain the property of
their respective authors and are not relicensed by the repository GPL-3.0-only
license.
No FallUI fonts, icons, FLA projects, or compiled SWF files are included.

### Prisma UI 2.0

`ui/prisma/views/csf_condition_widget.html` is an original CSF view that uses
the Prisma UI F4 bridge. The Prisma UI framework is not vendored. Obtain it
from its [official repository](https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/Prisma2.0),
where it is distributed under its own `LICENSE.md` terms.

## Vendored source

### Xbyak

The `src/xbyak` directory contains Xbyak by MITSUNARI Shigeo. Xbyak is
distributed under the 3-Clause BSD License. Its original copyright and license
text are retained in `src/xbyak/COPYRIGHT`.

### Prisma UI F4 API header

`src/PrismaUI_F4_API.h` is the public modder API header distributed by Prisma
UI F4 for copying into consumer projects. Prisma UI F4 itself is not included.

### ItemIntegrationFramework API header

`src/IIF_API.h` is maintained by the same project author and is included so
the optional ItemIntegrationFramework integration can be built independently.

## CommonLibF4 and commonlib-shared

This project is built against
[Dear-Modding-FO4/commonlibf4](https://github.com/Dear-Modding-FO4/commonlibf4)
at commit `ca31eeb6c7353555973bc351c6733d6492f2c66e`. CommonLibF4's top-level
code is distributed under the MIT License.

That revision statically links
[Dear-Modding-FO4/commonlib-shared](https://github.com/Dear-Modding-FO4/commonlib-shared)
at commit `f0b1670ee9caac2e349497f6f3c08a69633a8ea7`. `commonlib-shared` is
distributed under GPL-3.0 with a Modding Exception. The exception permits
Modded Code to link with `commonlib-shared` without causing that Modded Code to
be covered by the GPL. Project-authored source in this repository therefore
is distributed under the repository GPL-3.0-only license.

Copies of the applicable GPL-3.0 text and Modding Exception are retained in
`licenses/commonlib-shared/LICENSE` and `licenses/commonlib-shared/EXCEPTIONS`.

## Build dependencies

The following projects are obtained separately by the build system and are not
vendored in this repository:

- Dear ImGui, MIT License.
- Microsoft Detours, MIT License.
- MinHook, 2-Clause BSD License.
- SimpleIni, MIT License.
- JSON for Modern C++, MIT License.
- TinyXML-2, zlib License.

Refer to each dependency's source distribution for its complete license text.
