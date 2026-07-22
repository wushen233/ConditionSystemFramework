# UI source

This directory contains the source for ConditionSystemFramework's Scaleform
and Prisma UI integrations. Compiled game assets are intentionally excluded.

## Condition widget

`actionscript/condition-widget` contains the Adobe Animate project and AS3
classes for the Scaleform condition widget. Publishing `ConditionWidget.fla`
produces `ConditionWidget.swf` for `Data/Interface`.

## Jury-rigging menu

`actionscript/jury-rigging-menu` contains:

- `JuryRiggingMenu.fla`: Adobe Animate project.
- `JuryRiggingMenu.as`: current AS3 document class.
- `JuryRiggingMenu.legacy.as2`: retained earlier AS2 implementation.

Open the FLA in Adobe Animate 2024 and publish it for ActionScript 3.0. The
resulting `JuryRiggingMenu.swf` belongs under `Data/Interface` in a packaged
mod, but compiled SWFs are not tracked in this source repository.

## FallUI menu patches

`actionscript/fallui-patches/Scripts` contains replacement ActionScript files
with CSF's condition display, repair controls, and navigation integration:

- `ExamineMenu.as`
- `Pipboy_BottomBar.as`
- `QuickContainerItem.as`
- `QuickContainerWidget.as`

These are integration sources, not standalone FLA projects. Use them with the
matching Fallout 4/FallUI menu projects and their original assets. The repo
does not redistribute FallUI fonts, icons, media, FLA files, or compiled SWFs.

## Prisma UI widget

`prisma/views/csf_condition_widget.html` is the CSF condition widget loaded by
[Prisma UI 2.0](https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/Prisma2.0).
The Prisma framework itself is a separate runtime dependency and is not
vendored here.
