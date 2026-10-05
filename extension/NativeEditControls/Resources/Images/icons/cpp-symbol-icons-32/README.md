# C++ symbol icons — SVG export

Authored on a 16-unit grid (strokes are 1.25 units), with no fixed width or height so newui scales the viewBox to whatever size is asked for (16, 24, 32 ...). Colors baked in from the mockups.

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

## Explorer additions

Drawn for the project explorer in the same style (16-unit grid, 1.25 stroke, 14% tint), exported to the same `light/` and `dark/` layout with the family colors above.

| Folder | Icons | Family |
|---|---|---|
| `products/` | executable, static-library, shared-library, object-library, interface-library, test, custom-command | actions (executable, custom-command), collections (libraries), alias (interface-library), field (test) |
| `files/` | cmake, document, json, markdown, newui, image, data | macro (cmake), neutral (the rest) |
| `folders/` | folder, folder-open, source, include, tests, output | neutral |
| `sections/` | build-time, runtime, sources, dependencies | neutral |
| `info/` | info, timing, size, chart | actions |
| `tags/` | tag, tag-derived, tag-add | alias (tag, tag-add), template (tag-derived) |
| `badged/` (added) | source-notbuilt, source-unreferenced, header-unreferenced, cmake-notbuilt | the base's family |
| `badges/overlay/` (added) | not-built (a cross), unreferenced (a hollow ring) | `currentColor` |

Libraries are books, not boxes, so none reads as the cube that means `method`. `output` is dashed to say the folder is not source. One badge per icon: a file that is both not built and unreferenced gets the cross.

### Status badges (error, warning)

`badges/status/light/` and `badges/status/dark/` hold the bare marks: a red disc with a "!" for an error, an amber triangle with a "!" for a warning. Unlike the other overlays they keep their own colors (`#b91c1c` / `#f87171` and `#9a5b00` / `#f0b04a`, from the families above) instead of the base's, so severity reads the same on any file. The "!" is white on light and the panel color on dark.

Composed with the same knockout, in `light/badged/` and `dark/badged/`:

| Base | Badges |
|---|---|
| `source` | error, warning, notbuilt, unreferenced |
| `header` | error, warning, unreferenced |
| `cmake` | error, warning, notbuilt |
| `folder` | error, warning (a folder shows the worst of what is inside it) |

One badge per icon. Priority when several apply: error, warning, not built, unreferenced.
