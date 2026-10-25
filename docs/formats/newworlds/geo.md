# Star Trek: New Worlds: `.geo` models

Status: **core geometry understood** (faces, texture coordinates, vertices,
normals, edges); several blocks are still skipped. Worked out from complete
byte dumps of five small models, October 2026:

| Model | Bytes | What it is |
|---|---|---|
| `geo/rom/rb_mid.geo` | 980 | beam section: a two-sided rectangle |
| `geo/fed/fs_s.geo` | 1268 | flat outline: a hexagon on one side, 4 triangles on the other |
| `geo/kln/kwb_s.geo` | 1440 | two separate flat pieces |
| `geo/map/aln/concrete.geo` | 1452 | terrain tile: flat 8192 × 8192 square |
| `geo/map/aln/lod/alien_14.geo` | 1476 | terrain tile: square sloping up 3264 units |

Reader: `src/engines/newworlds/geo.cpp`. Check every model with
`reluva-tool geo-survey *.cat`; export one to OBJ with
`reluva-tool geo-info <file> <out.obj>`. Stored in `.cat` archives with chunk
tag `0x17`; the size in the archive's copied metadata never matches, so these
were presumably converted when packed.

All values little-endian; floats are IEEE 32-bit.

```
faces          face count × face; face 0's header is the file header:
                 u32 ?        file header: 952 / 416 / 1072 / 328 (fs_s, rb_mid, kwb_s, terrain)
                              other faces: the previous face's part
                 u32 ?        file header: the face count; other faces: up to
                              49 seen (perhaps a material index)
                 u32 corners
                 u32 part     which part the face belongs to (see Parts)
                 corners × 32 bytes: u32 vertex, f32 u, f32 v (both × 65536),
                 5 × u32 extra: extra[0] is the corner's normal, an index into
                 the part's second normal list (smooth shading); in the last
                 corner, extra[4] picks the texture
bounds         u32 last face's part, u32 ?, u32 part count,
               u32 attachment count (2 in rb_mid, 4 in fcarb, 6 in rms_d)
               f32 centre x, f32 centre y, f32 radius
attachments    u32 node count, u32 place count (the .mdl's first and third
               name lists), u32 ? (1 when there are attachments), then per
               attachment 58 bytes: 9 x f32 orientation and f32 scale, u16
               0x1c, u32 12, 3 x f32 position (see mdl.md). 58 isn't a
               multiple of 4, so what follows is often not 4-byte aligned
part data      u32 size (reaches the end of the file in mn_s), u32 1,
               u32 part count, then per part, in part order:
  (parts > 0)    u32 0        end of the previous part
  vertices       u32 ? (0; 1 in ptank_l's part 1), u32 count,
                 count × (f32 x, y, z)
  normal lists   repeated while present: u32 1, u32 count, count × unit vector
  edges          u32 0 = none (terrain tiles), or u32 1, u32 group count, then
                 per group: u32 count, count × (u32 part, v0, v1, faceA, faceB)
...            after the last part (mn_s): u32 2, then records of a position
               (a part 0 vertex), a vector (0, 0, −4096), u32 0, 3, 1, and an
               index (0, 4, 6); then u32 3, 2, 8, 9, 0, 0, 2 before the names
...            terrain tiles: u32 1, u32 64, then per cell of an 8 × 8 grid a
               count and that many face indices; then u32 8, 8, ?, bounds, and
               the faces again (in hole_02, with the same texture coordinates:
               not a second texture layer)
trailer        names ("main", "terminate", "muzzle_flash", "front_lock",
               "pickup"),
               layout not studied
```

## Parts

Some models are built from separate pieces, each with its own part number
(0, 1, …) in its faces' headers. `mn_s` has 2 parts, `fcarb` 3, `rms_d` 9.
Faces are grouped by part, though not in part order: `rms_d` goes 0, 6, 7, 5,
8, 4, 3, 2, 1. Each part numbers its vertices from 0: part 1 of `mn_s` uses
vertices 0–5, and part 0 uses vertices 0–10. In the corners of a part above 0,
the last u32 of the 20 trailing bytes holds the part number, except in the
face's last corner. Each part has its own vertices, normal lists and edges, stored one part
after another; each edge record starts with its part's number, and its faces
(numbered across the whole model) belong to that part. Part vertices are in
model coordinates: the two parts of `mn_s` (a flat outline: body and wings,
then the tail) meet without any offset.

## Textures

No file names a model's textures; they are found by name. `k_bop`
(`kliactor.cat`) goes with `lbp/txr/kln/k_bop_a`, `k_bop_b` and `k_bop_g`.
Each face picks one with the **last value of its last corner**
(`extra[4]` in the reader): in `k_bop`, 0 is the central body (`k_bop_a`),
1 both wings (`k_bop_b`), and 2 three spots on the body (`k_bop_g`, the
small 128 × 128 one). So the textures are numbered in name order.

`reluva-tool geo-textures <file.cat>...` applies the rule below to every model
and lists the ones it finds no textures for (`-a` lists every model's).
Known misses: textures look misplaced on some models, such as `fpdt` and
`fhf_p`.

Buildings also use **shared textures** with names of their own:
`fedbuild.cat` has `pad`, `door2`, `door3`, `pdoor`, `f_in2`..`f_in4`,
`deckanim` and `webspark`; `klibuild.cat` the Klingon equivalents (`kcdoor`,
`kdoor_h`..`kdoor_h3`, `kdeckan`, `k_in1`, `k_in2`, …). No file lists which
shared textures a model uses: their names appear in no other file (the
`.mdl` files list docking points and states, `constructed` and
`deconstructed`, but no textures), and the archive's order doesn't follow
the models.

Comparing the two archives (October 2026) placed one of them, by the shape
of each number's faces (`reluva-tool geo-slots <file.geo>` lists faces, area,
facing and texture-coordinate range per number):

- **Own textures come first, in name order, `_g` included**: `frps_a`,
  `fss_a` and `kch` number `frps`, `frps_g` (six roof faces), …; the `_g`
  isn't the last number, as first thought.
- **Doors:** nearly every building has a number used by one or two faces,
  at least 500 units across and not facing up, each showing the whole
  texture: the door, as the last number or near it (`fch`, `accom`,
  `landpad`, `kab`, `kief`, `ksc`, `klf`, `kvy`, `krps`, `kcy`, `kch`, …).
  A face showing a whole texture can't use the 4 x 4 sheets of opening
  frames (`kdoor_h`, `door2`, `pdoor`), so it's each side's single closed
  door: `kcdoor` (64 x 32) and `door3` (32 x 32). Pure name order is ruled
  out: `kcdoor` would sort before `kief` and `ksc`, but is their last number.
  A model can also skip one of its own textures: `fch`'s second number is its
  door, not `fch_g`.
- **Aprons:** many Federation buildings have one or two up-facing faces
  showing a quarter or the whole of a texture (in front of `frps_a`'s door,
  on `fsc`, `foodstor`, `fcy`, `fvpy_a`): **`pad`**. Its colour is plain
  yellow, but its alpha layer holds four panels of hazard stripes (one with
  arrows), one per quarter, matching those faces' texture coordinates; drawn
  with the alpha as transparency they are the striped aprons of the
  building portraits in `fedgui.cat` (see [gui.md](gui.md)). So outside the
  `_g` textures, alpha is transparency.
- Faces about 250 units across showing a whole texture (`fsc`, `fss_a`) are
  something smaller: lights or signs.
- In play ([play-f1.md](play-f1.md#buildings-and-their-models)), `fss_a`'s
  side panels, which no texture here is known for, are grey with two blue
  bands, like panels of the `fss` texture; and buildings grow by added
  pieces (`fvpy_b`/`_c` sections, `frps_b`/`_c` arms, `fch_b` and `fch_c`,
  `fss_b` wings).

`rombuild.cat` and `forbuild.cat` show the same structure: the Romulan,
Metar, Taubat and Hubrin buildings also end in a door number (the last
number in 48 of the 49 Klingon, Romulan and native buildings that have one;
the Federation's 19 put an apron after it in 8). Their door textures are
all sheets of frames (`r_door`, `rvdoor`, `dooranim`, `m_door`, `t_door3`),
so either the game shows door faces one frame at a time, which would make
the Federation's and Klingons' doors `pdoor`/`door2` and `kdoor_h`…
rather than the single pictures, or their doors are something else. The
viewer leaves those doors untextured. A texture named after the model with
`_door` (`mch_door`, the Metar colony hub's) is that model's door and
doesn't take a number in the name order (`mch`: `mch`, `mch_g`, ?,
`mch_door`).

The rest of the shared numbers stay unknown and are drawn untextured.

**Weapon effects** (in the `*misc` archives: beams, muzzle flashes,
impacts, shields) name their textures their own way: beam segments
(`mb_mid`, `_midh` head, `_midt` tail, `_midht` both) use the side's beam
(`m_beam`, `t_beam`, `r_beam`; the Federation's `ph_mid*` use `phaser`), a
muzzle flash the folder's muzzle texture with the same first letter
(`mmuzzle`: `mbmuzzle`, `rmuzzle`: `rb_muzzl`), a shield its ripple
(`fshield`: `fripple`), and `kdisrupt` `disrupt`. Names a number apart or
shortened also match (`fapc2`: `fapc`; `fconbee`: `fconb`; `r_bop`:
`r_bop5`-`7`), a low-detail model taking the `_l` one.

**Star systems** (`glblgame.cat`, `geo/solsys/`) are one model per
system, and `tabras` (Tabula Rasa) is the galaxy; see
[Star systems](#star-systems). `tabras` is the galaxy of the
briefings' STELLAR CARTOGRAPHY step: a flat slab (20 faces, texture 0) round
three flat layers (textures 1-3), numbering `tabras01`-`04` in name order:
a star field, a blue arm of stars, the red ribbon and a scatter of stars.
Their alpha layers are their brightness; drawn adding their light on black
and seen from above at an angle, they look like the recordings' galaxy.

**Whose buildings:** `forbuild.cat` (folder `bad`) holds the native races'
buildings, named by their metamodels in `misc.cat` (`mdl/bad/`): `h…` the
Hubrin (`hcp` containment pillar; `hf` an "ancient fortress"), `m…` the
Metar (`mch` colony hub, `mport` portal; their units `mb`, `md`, `mj`, `mn`,
`mr`, `my` and the starship `mss`), `t…` the Taubat (`th` house, `tt`
temple, `tw` workshop, `tsg` static gun), and one Orion tank (`odv`): the
sides `game.ini` numbers 4-6 (see [level.md](level.md)). The `_p` models
are the empires' building **portals** (`fedconyardportalmetamodel`) and the
natives' **interiors** (`taubattempleinteriormetamodel`); which textures
they use isn't known (the Romulans have a `portal1`).

`reluva-tool geo-sheet [-l|-t] <out.ppm> <filter> <file.cat>...` draws every
main model whose name holds the filter to a contact sheet, textured by the
rule below, two views each (`-l` four, `-t` eight round it at the
briefings' height): the way the recordings' models were matched.

The viewer's rule: a model uses the textures named exactly after it or after
it plus `_` and a suffix, in name order from 0, preferring its own folder
(`kln`). A model without any, such as a `_l` version, uses those of its name
without the suffix, and a shortened name such as `landpd_d` those of the
texture it abbreviates (`landpad`). `_s` textures belong to the `_s` models.
Then door numbers (as above) get the side's door, and unfilled apron numbers
(one or two up-facing faces) the side's `pad`. A model without a match at
all, such as the `_s` outlines, is drawn untextured.

In the other corners of a face, the same value holds the part number (0 in
part 0), so it isn't a per-corner texture choice.

Variants share textures: the `fedbuild.cat` models `frps_a`, `frps_b`,
`frps_c` and their damaged versions `frps_ad`, `frps_bd`, `frps_cd` all use
`lbp/txr/fed/frps` and `frps_g`.

**`_g` textures** (`k_bop_g`, `frps_g`, `kcs_g`, …) have an alpha layer.
Treated as transparency, it makes parts of hulls see-through, and more of
the damaged versions, so it is probably something else, such as a glow or
lighting map. The viewer's G key switches between ignoring it, glow and
transparency. Not settled.

**Ground tiles** (`concrete`, `alien14`, …) find no textures this way. The
planet archives hold terrain textures such as `lbp/txr/map/alp/green01` and
`cliff01` (in `alpwrld.cat`, next to the `.gmf` maps), so the maps
presumably assign them.

## Star systems

`glblgame.cat`'s `geo/solsys/` holds 23 systems (`alfadir`, `ap173`,
`bezouk`, `bursai`, `cadimus`, `durac`, `fico`, `fthorc`, `gestas`,
`hrimfrax`, `ixion`, `jeddart`, `jotunhei`, `karazin`, `lupercal`,
`mazikaan`, `pellion`, `sakuntul`, `taal`, `tancred`, `taphet`, `tarikon`,
`tybalt`), named after the system or a planet in it (`karazin` is F1's
planet of operations; its system has another name). Each model is, in one part:

- **A starfield:** a sixteen-sided cylinder round everything (about 7800
  units across, 3100 high), its walls numbering four textures in order
  (usually 2-5) and its ends 0. They're the four `solsys/stars1`-`4`
  pictures, whose alpha layers are their stars' brightness.
- **The star:** a stack of seven square slices, every face numbered 0
  (`karazin`'s at the middle, about 150 units across).
- **Planets:** stacks of seven square slices like the star, about 120-370
  units across, each slice twice: a quad numbered with the planet's own
  texture and two triangles over it, its back, numbered 0. A ringed planet
  has six slices and a ring, a bigger slice numbered on its own.

The pictures (`lbp/txr/solsys/`, 32 × 32, rings 64 × 64) are discs: their
alpha layers are 0 outside a circle (rings: outside a ring), and most are
light on one half and dark on the other, a planet's day and night. A
stack of them turned to face the eye is a ball. The planets are `ice`,
`desert`, `alpine`, `alpine2`, `red`, `volcanic`, `volcani1` and
`alien1`-`7`, with rings `alien3_r`, `alien7_r` and `red_r`. The stars are
`sun`, `sun2` and `sun3`.

**Which system:** each mission's string table names its system's planets,
most often the one whose model shows: F1 `karazin`, F2 `jeddart`, F3
`fico`, F8 `durac`, F12 and F14 `tybalt`, K1 `taal`, K2 `ixion`, K7
`bursai`, K8 `tancred`, K12 and K14 `fthorc` (spelt with an apostrophe),
R1 `sakuntul`, R2 `taphet`, R8 `hrimfrax`, R9 `tarikon`, R12 and R14
`bezouk`, and the joint missions J1 `mazikaan`, J2 `jotunhei`, J3 `ap173`,
J6 `pellion`, J7 `lupercal`, J8 and J9 `cadimus`. J4's system has no model
of its name (its recording shows a yellow haze), and `alfadir` and `gestas`
no mission.

**Which textures** isn't in the files: no texture is named after a system,
the metamodels (`mdl/solsys/`) name no textures, and nothing else in the
archives mentions the pictures; it's presumably in `STNW.exe`. The
recordings' star system steps show, small: a yellow star in every one;
F1 a cyan planet (Karazin is an ice world); J1 a cyan planet held in the
middle (an ice world); J3 a green one (alpine); F12 and F14 a green one
(alpine); F8 red, magenta and tan planets; J8 teal, cyan and green ones.
The Romulan recordings add: R1 (Sakuntul) a dull red star with yellow,
teal, green and two magenta planets; R12 and R14 (Bezouk) a red star with
a red ring and a banded orange planet; J1 a yellow body and a cyan planet;
J2 a yellow-green banded body with magenta and cyan ones; J3 an orange-cream
one; R9 and J7 a big mottled orange body; J4 only a yellow haze (its system
has no model); J9 shows no system at all (a ground view instead).
So Reluva's choice: `sun` for the star (`sun3`, the reddest, for Bezouk's
and Sakuntul's), the mission's world for its first
planet (by texture number: `ice`, `desert`, `alpine`, `red`, `volcanic`,
`alien4` for the alien world), the matching planet and ring for a ringed
one (`red`/`red_r`, `alien3`/`alien3_r`, `alien7`/`alien7_r`), and the
rest from a fixed list starting with `red` and `alien2` (magenta), and
`stars1`-`4` for the walls (`src/engines/newworlds/star_system.h`).

## Winding and file names

**Front faces are clockwise.** Drawn with x, y, z as a right-handed, z-up
space, a face's corners run clockwise when seen from outside the model.
Hiding faces that appear anticlockwise on screen leaves solid models such as
`k_bop` whole, while hiding the clockwise ones empties them. That's the Direct3D
convention. Whether the game's space is mirrored relative to this one (which
would flip both the picture and the winding) isn't confirmed yet.

**Name suffixes** (from looking at the models in the viewer):

| Suffix | Meaning |
|---|---|
| `_l` | lower-detail version (e.g. `phtart_l`, `ptank_l`, `mb_l`) |
| `_s` | flat outline at z ≈ 0, presumably the shadow (`fs_s`, `kwb_s`, `mn_s`) |
| `_p` | a separate piece of the main model: `fhf_p` looks like the inside of the `fhf` hangar's bay, seen when its doors open |
| `_d` | probably *deconstructed*: buildings' `.mdl` files list the states `constructed` and `deconstructed` |
| `_a`, `_b`, `_c`, `_r` | variants with small changes; `_ad`, `_bd` are their `_d` versions |

## Evidence

- **Face 0 is in the file header.** In `concrete` the header's `2, 3` are 2
  faces and 3 corners, and the first 3 records form a triangle (1, 3, 0) with
  the second triangle (2, 0, 3) after it. Read that way, `rb_mid` is a
  rectangle (3,1,0,2) and its back (3,2,0,1), and `fs_s` is a hexagon and the
  four triangles of its other side.
- **Corners** carry their own texture coordinates; in `rb_mid` they form a
  consistent planar mapping (u along y, v along x) only if the first field is
  the vertex index.
- **Edges** name two vertices and the two faces either side, 0-based. Every
  edge in all five models passes the reader's check that both vertices lie on
  both faces: e.g. all four edges of `rb_mid` lie between face 0 and face 1
  (front and back), and `fs_s` has the 9 edges between its hexagon and its 4
  triangles.
- **Edge groups**: `kwb_s` has two groups of 5 edges, one per piece
  (vertices 0–3 with faces 0–2, and 4–7 with faces 3–5). Only with two groups
  does it show that each record starts with the 0 rather than ending with it.
- **Unaligned blocks**: in `fsb_b` (7 faces) the attachment data contains
  the bytes `1c 00 0c 00` and ends at an odd offset; the vertex block's
  `1, 1, 0` sits at 0x416 and its count of 8 at 0x422. The reader therefore
  searches for the vertex block byte by byte.
- **Radius** is the farthest vertex from the centre: 488.2 in `fs_s` is the
  distance to (−88.6, −480.1).

## Open questions

- What parts are for (moving pieces? damage states?), and the data after
  the last part.
- The file header's first value, face header values, and the 20 bytes after
  each corner's texture coordinates (sometimes a 1 in the last word).
- The terrain grid's exact layout; the second texture set in
  terrain tiles; the trailer.
- The first normal list: one entry per distinct face direction, probably
  face normals (`hole_01`: 13 for 16 faces), but no face field is known to
  index it.
- Larger 3D unit models haven't been checked yet.
