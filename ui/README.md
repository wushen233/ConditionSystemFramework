# UI source

This directory contains the source for ConditionSystemFramework's Scaleform
and Prisma UI integrations. Compiled game assets are intentionally excluded.

## Condition widgets

The Scaleform HUD uses four standalone widget movies. The CND widget has one
FLA per visual style, while heat and unjam are independent document classes:

- `condition-cnd-widget-fnv/ConditionCndWidgetFNV.fla`
- `condition-cnd-widget-fo4/ConditionCndWidgetFO4.fla`
- `condition-heat-widget/ConditionHeatWidget.fla`
- `condition-unjam-widget/ConditionUnjamWidget.fla`

`actionscript/condition-heat-widget` contains the standalone heat-widget
document class and its Animate FLA. Its normal heat fill is driven by the
runtime HUD color; overheat flashing remains separate. Publishing it produces
`ConditionHeatWidget.swf` for `Data/Interface`.

The FNV and Fallout 4 CND projects publish the matching style directly.

Each split FLA has its own complete `ConditionWidget.as` and
`DurabilityWidget.as` source seam because the Animate timeline binds the
document class by its exact `ConditionWidget` name. The two outputs expose the
same native entry points, while their default style is fixed by the selected
source seam. The MCM FO4/FNV positioners and the native CND menu now select the
matching SWF directly. The old archived `ConditionCndWidget` material remains
reference-only.

The archived `archive/ui/condition-cnd-widget/ConditionCndWidget.fla` remains
visual reference material only. The split CND FLAs use the normalized 1280x720 stage and the recovered
positions: CND text at `y=2`, bar at `y=8`, and multiplier at `y=3`. Do not
publish the archived FLA or SWF directly: their historical document class,
output name, and saved stage geometry are intentionally kept for comparison
only.

The workspace project manifest at
`projects/ConditionSystemFramework/Scripts/swf-publish-manifest.json` is the
authoritative FLA selection list. Historical recovery copies are kept outside
this source directory under the project `archive/` directory and are not
publish inputs.

## Jury-rigging menu

`actionscript/jury-rigging-menu` contains:

- `JuryRiggingMenu.fla`: Adobe Animate project.
- `JuryRiggingMenu.as`: current AS3 document class.
- `JuryRiggingMenu.legacy.as2`: retained earlier AS2 implementation.

Open the FLA in Adobe Animate 2024 and publish it for ActionScript 3.0. The
resulting `JuryRiggingMenu.swf` is staged under the project
`data/Interface/` image by the project publish manifest. The legacy AS2 file
is retained as historical source only and is not a publish input.

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
