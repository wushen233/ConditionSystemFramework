# Third-Party Notices

This repository contains the independently maintained native C++ backend,
public integration headers, configuration examples, and documentation for
ConditionSystemFramework. It does not contain FallUI assets, modified FallUI
ActionScript, FLA projects, compiled SWF files, plugin records, Papyrus
binaries, or other compiled game assets.

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

## Build dependencies

The following projects are obtained separately by the build system and are not
vendored in this repository:

- CommonLibF4 by Dear Modding, MIT License.
- Dear ImGui, MIT License.
- Microsoft Detours, MIT License.
- MinHook, 2-Clause BSD License.
- SimpleIni, MIT License.
- JSON for Modern C++, MIT License.
- TinyXML-2, zlib License.

Refer to each dependency's source distribution for its complete license text.
