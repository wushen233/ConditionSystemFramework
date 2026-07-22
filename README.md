# ConditionSystemFramework

Native F4SE/CommonLibF4 durability, weapon-condition, armor-condition, and
repair framework for Fallout 4.

Mod page and downloads: [ConditionSystemFramework on Nexus Mods](https://www.nexusmods.com/fallout4/mods/105673)

## Source scope

This repository contains the independently maintained C++ backend, public API
headers, configuration examples, and developer documentation.

It does not include:

- ActionScript source or modified FallUI ActionScript
- Adobe Animate FLA projects or compiled SWF files
- ESP/ESL/ESM plugin records
- compiled Papyrus scripts
- MCM or Prisma UI runtime pages
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
- Optional ItemIntegrationFramework and Prisma UI F4 integration
- Multiple Fallout 4 runtime generations through CommonLibF4

## Requirements

- Windows 10 or later
- Visual Studio 2022 Build Tools with Desktop development with C++
- Git
- XMake 3.0 or later
- Fallout 4 Script Extender

Runtime integrations such as ItemIntegrationFramework and Prisma UI F4 are
optional and are discovered by the plugin at runtime.

## Build

The build script downloads the pinned CommonLibF4 revision into `.deps/` when
needed, configures XMake, and builds the plugin:

```powershell
.\scripts\build.ps1
```

To use an existing CommonLibF4 checkout:

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

The original project source is released under the MIT License. Vendored Xbyak
files remain under their original BSD-3-Clause license. See
`THIRD_PARTY_NOTICES.md` for details.
