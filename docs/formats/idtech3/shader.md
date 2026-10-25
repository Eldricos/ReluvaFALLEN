# id Tech 3: shader scripts (`scripts/*.shader`)

Status: **every Quake III directive parsed and drawn** except those listed
at the end (`src/engines/idtech3/shader.cpp`, `world_renderer.cpp`).
Checked on generated test maps and on the retail Elite Force maps, which
look as in the original. `reluva-tool shader-check` on Elite Force
(October 2026): 1872 shaders, **no directive outside Quake III's set**;
not drawn yet are `deformVertexes autosprite2` (2 shaders,
`textures/scavenger/fire1` and `fire2`), `deformVertexes projectionShadow`
(1, for models' shadows) and portals/mirrors (10, e.g.
`textures/common/mirror1`, `textures/common/portal`).

Sources: the Quake III Arena shader manual id Software published for map
makers, and the GPL Quake III Arena source (`code/renderer/tr_shader.c`)
for how the engine reads it.

## Layout

Tokens as in [text.md](text.md). A file is a list of shaders:

```
textures/borg/floor          // the name: an image path without extension
{
    surfaceparm metalsteps   // shader-level directives, one per line
    {                        // a stage: one rendering pass
        map $lightmap
    }
    {
        map textures/borg/floor.tga
        blendFunc filter
    }
}
```

When several files define a name, the first one read wins. Reluva reads
the files in name order. Names are looked up without their extension, as
the original does: a model surface naming `models/x/y.tga` uses the script
`models/x/y` if there is one, and only otherwise the image.

A surface whose shader name has no definition is drawn with the image of
that name (`.tga`, else `.jpg`) multiplied by its lightmap.

## How the renderer draws a shader

Read from the GPL Quake III Arena renderer (`tr_shader.c`,
`tr_shade_calc.c`, `tr_sky.c`, `tr_bsp.c`) for behaviour; Reluva's code
(`src/engines/idtech3/world_renderer.cpp`) is written from this description.

- **Stages** are drawn in order, each a pass over the surface.
- **Depth:** a stage writes depth unless it blends (anything but
  `GL_ONE GL_ZERO`), or unless it says `depthWrite`. `depthFunc equal`
  draws only where the depth matches.
- **Colour:** without `rgbGen`, white. Without `alphaGen`, opaque.
- **Texture coordinates:** without `tcGen`, the lightmap coordinates for a
  `$lightmap` stage and the texture coordinates otherwise. `tcMod`s apply
  in the order written.
- **Lightmaps:** `$lightmap` on a surface without a lightmap is white.
  Lightmaps and vertex colours are stored at half brightness: on loading,
  each colour is doubled, and a component over 255 scales the whole colour
  down to keep its hue. (The original doubles or quadruples, depending on
  its gamma mode; doubling matches its windowed look.)
- **Surfaces without a script** get implicit shaders: with a lightmap, the
  lightmap then the image of the shader's name with `blendFunc filter`;
  without one, the image times the vertex colours. Triangle soups always
  use their vertex colours. Any negative or out-of-range lightmap number
  means vertex colours.
- **Sort order** (lower draws first): `sort` if given (portal 1, sky 2,
  opaque 3, decal 4, seeThrough 5, banner 6, underwater 8, additive 10,
  nearest 16, or a number). Otherwise portal 1, `polygonOffset` 4, and
  if the first stage blends and some stage does, 5 when that stage writes
  depth or 9 when it doesn't; everything else 3.
- **Sky:** the far box is drawn on a box around the camera, the images
  `<name>_rt _bk _lf _ft _up _dn` on its sides. The sky shader's stages
  are the cloud layers, drawn over every side but the bottom. Their
  texture coordinates come from where each direction meets a dome
  `cloudHeight` (default 512) above the camera on a world of radius 4096:
  the arc cosines of the normalized hit point's x and y.
- **Fog:** a surface in a fog volume gets a last pass of the volume's
  `fogParms` colour, more opaque with distance, fully at its distance.
  Reluva uses the distance from the camera; the original also accounts
  for how deep in the volume the surface and the camera are.

## Interpreted

| Where | Directive | Meaning |
|---|---|---|
| shader | `surfaceparm <name>` | a flag for the compiler and game; `nodraw` surfaces aren't drawn |
| shader | `skyParms <farbox> <cloudheight> <nearbox>` | the shader is a sky; `-` for none |
| shader | `fogParms ( r g b ) <distance>` | a fog volume |
| shader | `cull front\|back\|none` | which side is hidden (`disable`, `twosided` = none) |
| shader | `deformVertexes wave <div> <func> <base> <amp> <phase> <freq>` | along the normals, phase offset by (x + y + z) / div |
| shader | `deformVertexes bulge <width> <height> <speed>` | along the normals by height * sin(s * width + time * speed) |
| shader | `deformVertexes move <x> <y> <z> <func> ...` | the whole surface by the vector times the wave |
| shader | `sort`, `polygonOffset`, `nomipmaps`, `nopicmip`, `portal` | see above |
| stage | `map <image>` / `clampMap <image>` | the stage's texture; `$lightmap` is special, and `$whiteimage` and `*white` both name a plain white image |
| stage | `animMap <fps> <image>...` | frames played at `fps` |
| stage | `blendFunc add\|filter\|blend` or `<src> <dst>` | `GL_ONE GL_ONE`, `GL_DST_COLOR GL_ZERO`, `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA`, or explicit factors |
| stage | `rgbGen identity\|identityLighting\|vertex\|exactVertex\|oneMinusVertex\|wave ...\|const ( r g b )\|entity\|oneMinusEntity` | the colour (lightingDiffuse uses the vertex colours for now) |
| stage | `alphaGen vertex\|oneMinusVertex\|wave ...\|const <a>` | the alpha (`portal`, `lightingSpecular`, `entity`: opaque for now) |
| stage | `tcGen base\|lightmap\|environment\|vector ( ) ( )` | where texture coordinates come from |
| stage | `tcMod scroll\|scale\|rotate\|stretch\|transform\|turb\|entityTranslate` | texture coordinate animation, in order |
| stage | `alphaFunc GT0\|LT128\|GE128`, `depthFunc`, `depthWrite`, `detail` | see above |

Waves are `<func> <base> <amplitude> <phase> <frequency>`, func one of
`sin`, `triangle`, `square`, `sawtooth`, `inversesawtooth`, `noise`.

Not drawn yet: `deformVertexes autosprite`, `autosprite2`, `text*` and
`projectionShadow`; portals and mirrors (drawn as plain surfaces);
`GL_SRC_ALPHA_SATURATE`. `reluva-tool shader-check <game folder>` lists
what a game's scripts use that isn't handled, including directives the
parser doesn't know (game additions).
