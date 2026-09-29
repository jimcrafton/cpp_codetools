# C++ symbol icons

82 SVGs for outline rows, hover, diagnostics, statements, file kinds, editor actions and standard containers. Designed on a
design canvas (Claude artifact "C++ Symbol Icons", boards: Icon set / In an outline / Statements and
flow / Access and modifiers / Diagnostics and types / More statements / Files and actions / Collections). The
`.svg` files here are the source of truth from now on; the canvas is only a preview.

## Format

- 16x16 `viewBox`, no fill on the root, `stroke="currentColor"`, `stroke-width="1.25"`, round caps and joins.
  Same shape as the other icons under `Images/icons/`. 1.25 was chosen deliberately over 1.6: the
  small marks (badges) fill in at 1.6. Do not "normalise" it to match the older icons.
- Tinted areas use `fill="currentColor" fill-opacity=".14"` (or `.5` for a header band), so one color
  drives the whole icon.
- Shape carries the meaning; color only reinforces it. Families: callables violet, fields/variables
  green, class blue, struct teal, union pink, enum orange, template/concept/TODO amber, control flow
  cyan, exceptions/errors red, new/delete pink, type kinds slate, file-level neutral, actions indigo.
  The hex values on the canvas are placeholders. Map them to `UIColorRole` names before use.
- File names: kebab-case (`static-assert.svg`). Badge overlays are `badge-*.svg`.

## Badge overlays (access and modifiers, constructor, destructor)

12 files: `badge-public/protected/private/static/virtual/override/const/constexpr/deleted/defaulted/ctor/dtor.svg`.

An overlay is a transparent 16x16 canvas containing **only** the mark, drawn in the bottom-right
**badge zone**. It has no background and no halo. Its color is `currentColor`, so it inherits the base
icon's color.

**Badge zone** (fixed for every badge, not per-badge): circle, center (12, 12), radius 3.4, in the
16x16 viewBox. Every mark must fit inside it, and the file may not draw outside it.

### Composition algorithm

Compose at the icon's target pixel size `S` (scale `k = S / 16`). Do it on an offscreen image, never
directly on the destination, because the erase step must not touch what is behind the icon.

1. Rasterize the **base** SVG into a `BLImage` (`BL_FORMAT_PRGB32`, `S x S`) with
   `newui::renderSvgFile()`, tinted with the icon color (see open question 1).
2. Open a `BLContext` on that image. Set `BL_COMP_OP_DST_OUT`. Fill the badge-zone circle
   (`cx = 12k, cy = 12k, r = 3.4k`) with opaque black. This erases a hole in the base. Edge pixels
   are partially erased, so the cutout is anti-aliased.
3. Rasterize the **overlay** SVG into a second `S x S` `BLImage`, same tint, and draw it onto the
   first with `BL_COMP_OP_SRC_OVER` at (0, 0).
4. Draw the composed image onto the real target with `SRC_OVER`.

Why erase instead of a filled halo: the hole shows whatever is really behind the icon (selected row,
hover, dark theme) with no background color needing to be known. The overlay files stay theme-free.

### Rules

- **One badge per icon.** At 16 px a second badge is unreadable. Extra modifiers belong in the row's
  text, not the icon.
- Apply to any base whose bottom-right corner is not load-bearing. Confirmed on the canvas for
  method, field, variable, function, class, struct and enum. Still unchecked: the badge sitting on
  union, typedef, template. Check by eye before enabling for a new base.
- Composed results should be **cached** per (base, badge, size, color). Composing per paint is
  wasteful and the inputs almost never change.
- Constructor and destructor are overlays too: `method.svg` + `badge-ctor.svg` / `badge-dtor.svg`.
  There is deliberately no `constructor.svg` or `destructor.svg`.
- Suggested API shape (not written yet): `IconStore::get(name, badge, sizePx, color) -> const BLImage&`
  with the cache inside it.

### Open questions (verify against newui before writing the helper)

1. **How `currentColor` is resolved today.** `renderSvgFile()` (svgimage.h) rasterizes through
   svgandme and takes no color argument. The existing icons (e.g. `find/close.svg`) also use
   `currentColor`, so there is some tinting path already. Find it, and reuse it for both base and
   overlay so they always get the same tint. If none exists, either pass a color into the rasterizer
   or rasterize white and tint with a `SRC_IN` fill.
2. **DPI.** Rasterize at physical pixel size (after DPI scaling), not logical, or the 1.25 stroke and
   the small marks blur. Follow whatever `DisplayMetrics` does for the Find/Replace icons.
3. Whether `renderSvgFile()` handles `stroke-dasharray` (used by typedef, try, delete, module,
   typeparam, extract). Check those six render dashed and not solid.

## Not done yet

- The compose helper and cache (above).
- Adding these files to CMake / the resource bundle list, if that list is explicit.
- Light/dark rows for the newer icons on the "In an outline" board.
- Mapping the placeholder colors to `UIColorRole`.
