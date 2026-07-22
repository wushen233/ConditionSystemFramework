# ConditionSystemFramework

Native F4SE/CommonLibF4 durability, weapon-condition, armor-condition, and
repair framework for Fallout 4.

Mod page and downloads: [ConditionSystemFramework on Nexus Mods](https://www.nexusmods.com/fallout4/mods/105673)

## Source scope

This repository contains the independently maintained C++ backend, public API
headers, configuration examples, developer documentation, ActionScript UI
integration source, the jury-rigging menu Animate project, and the Prisma UI
condition widget.

It does not include:

- ESP/ESL/ESM plugin records
- compiled Papyrus scripts
- compiled DLL, PEX, or SWF files
- MCM package files
- fonts, icons, or other FallUI assets

Install the complete mod package from Nexus Mods for normal gameplay. The
public source license applies only to files contained in this repository.

## Features

- Weapon and armor durability tracking
- Condition-based damage and armor calculations
- Weapon jams and configurable wear behavior
- Loot condition initialization and over-repair support
- Repair kits and jury-rigging systems
- JSON-driven weapon, armor, ammo, modifier, exclusion, and repair rules
- Public durability interface for other F4SE plugins
- ItemIntegrationFramework integration and optional Prisma UI F4 integration
- Multiple Fallout 4 runtime generations through CommonLibF4

## UI source

- `ui/actionscript/jury-rigging-menu` contains the CSF jury-rigging menu AS3,
  its earlier AS2 source, and the Adobe Animate FLA project.
- `ui/actionscript/condition-widget` contains the Scaleform condition widget
  AS3 classes and Adobe Animate FLA project.
- `ui/actionscript/fallui-patches` contains the four menu scripts modified for
  CSF workbench buttons, repair navigation, and condition display integration.
- `ui/prisma/views` contains the CSF-authored condition widget for
  [Prisma UI 2.0](https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/Prisma2.0).

The FallUI patch sources are separated from original CSF UI code so their
provenance is explicit. They require the corresponding game/FallUI menu
projects and assets; those third-party assets are not included here.

## Requirements

- Windows 10 or later
- Visual Studio 2022 Build Tools with Desktop development with C++
- Git
- XMake 3.0 or later
- [Dear-Modding-FO4/commonlibf4](https://github.com/Dear-Modding-FO4/commonlibf4)
- Fallout 4 Script Extender
- [ItemIntegrationFramework](https://github.com/wushen233/ItemIntegrationFramework)

ItemIntegrationFramework is a required runtime dependency. Prisma UI F4 is an
optional integration and is discovered by the plugin at runtime.

## Build

This project must be built against the
[Dear-Modding-FO4 CommonLibF4 fork](https://github.com/Dear-Modding-FO4/commonlibf4).
The build script downloads the pinned revision from that fork into `.deps/`
when needed, configures XMake, and builds the plugin:

```powershell
.\scripts\build.ps1
```

To use an existing checkout of the same CommonLibF4 fork:

```powershell
.\scripts\build.ps1 -CommonLibF4Path D:\path\to\commonlibf4
```

Manual build:

```powershell
$env:COMMONLIBF4_PATH = 'D:\path\to\commonlibf4'
xmake f -P . -y -m releasedbg
xmake -P . -y ConditionSystemFramework
```

The dependency checkout must include its `commonlib-shared` submodule.

## Configuration development

Default profile files are under `config/ConditionSystemFramework`. Annotated
examples and bilingual configuration guides are under `docs/`. Runtime
packages place the profile directory at:

```text
Data/F4SE/Plugins/ConditionSystemFramework
```

The compiled plugin belongs at:

```text
Data/F4SE/Plugins/ConditionSystemFramework.dll
```

## License

Original CSF source is released under GPL-3.0-only. Built binaries also contain
`commonlib-shared` code distributed under GPL-3.0 with the CommonLib Modding
Exception. Vendored Xbyak files remain under their original BSD-3-Clause
license. Third-party portions and compatibility patch bases remain subject to
their respective terms. See `COPYRIGHT`, `THIRD_PARTY_NOTICES.md`, and
`licenses/commonlib-shared` for details.
