# C++ symbol icons — SVG export

32×32 px (16-unit grid scaled 2×, so strokes render at 2.5 px), colors baked in from the mockups.

- `light/` and `dark/` — every icon in its family color for that theme (subfolders: symbols, statements, diagnostics-types, files-actions, collections, badged).
- `light/badged/`, `dark/badged/` — method/field/variable/function/class/struct/enum with each modifier badge, knockout applied.
- `badges/overlay/` — the 12 bare badge marks in `currentColor`, for composing at runtime (erase circle c=12,12 r=3.4 from the base, then draw).

| Family | Light | Dark |
|---|---|---|
| namespace / file-level | `#6b6f76` | `#9aa0a8` |
| class | `#2563c9` | `#6ea1f5` |
| struct | `#0e7c7b` | `#45c4c0` |
| union / heap | `#b83280` | `#f07fbf` |
| enum | `#c2410c` | `#fb923c` |
| template / generic | `#a16207` | `#e0b040` |
| function | `#6d3fc4` | `#a98bf0` |
| field / variable | `#2f7d32` | `#6fcb73` |
| type alias | `#46647f` | `#8fb0cf` |
| macro / preprocessor | `#7a6f5a` | `#c4b89c` |
| control flow | `#0e7490` | `#3cc0d8` |
| exceptions / errors | `#b91c1c` | `#f87171` |
| warnings | `#9a5b00` | `#f0b04a` |
| actions | `#4d5bc4` | `#909cf5` |
| collections | `#8f5215` | `#d99a5c` |

Dark values for control flow, exceptions, warnings, actions and collections were not in the mockups and were derived to match.
