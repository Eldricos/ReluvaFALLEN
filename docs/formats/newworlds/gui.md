# Star Trek: New Worlds: the GUI archives

`fedgui.cat`, `kligui.cat`, `romgui.cat` (one per side) and `glblgui.cat`
(shared). Checked October 2026: they hold only pictures (`.lbp`, chunk tag
`0x16`; all 748 decode, see [lbp.md](lbp.md)), fonts ([fif.md](fif.md)) and,
in `glblgui`, sounds and `frend.stb`. No interface layouts: where the
pictures go on screen isn't in any archive (presumably `STNW.exe`).

| Archive | Pictures | Fonts |
|---|---|---|
| `fedgui` | 241 | `fepnttle`, `fe8pt`, `fe5pt` (×5) |
| `kligui` | 197 | `klpnttle`, `kl8pt`, `kl5pt` |
| `romgui` | 197 | `rmpnttle`, `rm8pt`, `rm5pt` |
| `glblgui` | 113 | `fedengft`, `feengft2`, `final12` |

## Pictures

Under `lbp/gui/<side>/`: buttons, frames and corners of the interface
(`corner`, `asybcrnr`, `opacbutt`, `ops2butt`, `basebut`, `vcr`, …), and
**portraits** under `ptrt/`, each small (`sm/`) and large (`lg/`, 64 × 60):

| Folder | What | Federation examples |
|---|---|---|
| `build` | buildings | `fhub`, `fca` (accommodation), `flp` (landing pad), `fcy`, `fvpy`, `frps`, `fss`, `fsb`, `fms1`, `fms2`, `fpgs1`, `fpgs2`, `fpds`, `fpdt`, `fsc`, `fsg`, `fhf`, `fsfr`, `f_hcp` |
| `upgde` | building upgrades | `frps_bs`-`frps_fs`, `fss_bs`-`fss_fs`, `fsb_bs`-`fsb_es`, `fhub_bs` |
| `craft` | vehicles | `fapc`, `fgs`, `fmsg`, `fpv`, `fsv` (the vehicles with voice sets, see [wav.md](wav.md)), `fconb` (the construction vehicle), `fwb`, `fcrgb`, `fptl`, `fus`, `fs`, … |
| `crew` | crew and races | `f_c1`-`f_c34`, `fairbk` (Admiral Fairbanks), `f_sarek`, `f_fox`; on every side `taubat`, `metar`, `hubrin`, `orion` |

The Klingon and Romulan sets are alike (`kchub`, `k_kirgat`, `k_plyr`;
`rchub`, `r_narok`, `jathek`, `r_plyr`). The portraits link the names:
`fhub` is the `fch` model, `fca` `accom`, `flp` `landpad`; a building's
upgrades are its `_b`, `_c`, … models (the resource processor's five,
`frps_bs`-`frps_fs`, match the tutorial's "upgrade it to include all 6 ore
processors").

The building portraits are rendered from the models with all their
textures, so they show what the files leave out: the Federation buildings'
yellow-and-black hazard-striped aprons, which identified the `pad` texture
(see [geo.md](geo.md#textures)).

`fedgui` and `glblgui` also hold 21 test pictures under another build path,
`game/stc/bin/rsc/res/kurtscou/` (`kscout1s`-`5l`, `ktank1s`-`5l`,
`ksmile_b`, `ksmile_m`).

## Emblem screens

`glblgui.cat` holds each side's emblem as a full-screen picture cut into six
tiles, `ufp001`-`006` (the Federation), `kliemp01`-`06` (the Klingon Empire)
and `romemp01`-`06` (the Romulan Star Empire): three across and two down,
256 + 256 + 128 by 256 + 224 pixels, so together the 640 × 480 screen. They
are the emblem screen that ends a briefing (seen in recordings of play,
[play.md](play.md)). The `f_win`, `k_win`, `r_win` and `f_lose`... sets
beside them are likewise six tiles each, presumably the mission won and
lost screens.


## The in-game interface

Each side's GUI archive (`fedgui`, `kligui`, `romgui`) holds the pieces its
in-game panels are built from, under `lbp/gui/fed/`, `kln/` and `rom/`:

| Piece | Size | What it is |
|---|---|---|
| `corner` | 12 × 12 | a panel's top-left corner, flipped for the other three |
| `border` | 16 × 12 | the top edge, tiled; flipped for the bottom, turned for the sides |
| `wallpapr` | 64 × 64 | the dark grid tiled inside a panel |
| `sqre_asy*` | | square panel pieces (not used yet) |
| `logo32`, `shldicon`, `pwricon`, `popnicon`, `closebut`, `vcr`, `basebut`, `opacbutt` | | the emblem, icons and buttons |
| `ptrt/crew/lg/f_c1`... (`k_c`, `r_c`) | 64 × 60 | crew portraits |
| `ptrt/crew/lg/taubat`, `metar`, `hubrin`, `orion` | 64 × 60 | the natives' portraits |

The pieces are format 6 pictures (palette indices and an alpha layer), and
the alpha layer smooths their edges: the Federation's corner is rounded,
the Klingons' cut across. Federation pieces are purple, Klingon ones
copper. Each side has its own fonts: `fepnttle`, `fe8pt` and `fe5pt`
(`kl...`, `rm...` for the others).

Where the panels sit, from recordings of play ([play.md](play.md)), on the
640 × 480 screen:

- **NEW WORLDS**, top right, about 100 pixels wide: the colony size (20/50
  at the start) and the six resources' stock (every recording started with
  500, 1000, 2300, 500, 4000 and 5000).
- A compass strip along the top centre, ticks every 5 degrees.
- **COMMUNICATIONS**, bottom centre, about 200 × 62: the sender's portrait,
  the sender's section, the message, and which message of how many.
- **TRICORDER**, bottom right: the map small, with "TERRAIN AT <a:b>" under
  it.
- **COMMAND**, a list of the mission's objectives, each marked complete or
  incomplete.

The titles are gold and the text blue. The missions page draws these (key
**I**) with the side's own pieces and fonts. The right-hand panels are made
wide enough for their text in the 5 pt font, a little wider than the
recordings' 100 pixels. Which crew portrait goes with which section isn't
known: Reluva hands the side's first portraits to Command, Operations,
Engineering, Science, Medical, Security, Intelligence and Communications in
that order, except the Romulans' (below).

The Romulan recordings ([play.md](play.md)) show the same panels in grey
with green and teal: teal text, a green Tricorder relief map, the
Romulan emblem behind the briefings. Matching each message's face to
`ptrt/crew/lg/r_c*` (a sliding comparison of the recordings' 64 × 64
portrait against each picture) gives: Operations `r_c17`, Engineering
`r_c19`, Science `r_c21`, Intelligence `r_c18` or `r_c13` (the two pale
faces match equally), Security `r_c20` in R2 but `r_c19` in J1. So a face
belongs to an officer, and a section's messages usually but not always
come from the same one; presumably the officer posted there (the STAFF
panel lists each officer's section and post). Reluva uses these for the
Romulans. The others: `r_narok`, `r_plyr`, `jathek`, and the natives'
`hubrin`, `metar`, `orion`, `taubat`; the Hubrin's messages show their
emblem, not a face (C11).

