# Star Trek: New Worlds: what to collect in the game

A checklist for playing the original game in the VM to fill the gaps the
recordings left (the open questions of the [play-*.md](play.md) pages and the
format pages, and the steps the mission scripts infer). Highest value first.

**Have running:** screen recording with sound (the briefings' clips are
matched by audio, so leave game sound on and talk over nothing), and the
Tricorder panel visible so its position readout is in every frame. Start
each mission from the campaign screen so the loading box and the whole
briefing are recorded; don't press NEXT during briefings.

**Conventions:** map positions `<a:b>` are eighths of a block, row first
(0-511 on a 64-block map; a block's middle is 8n + 4), as the Tricorder
shows them ([gmf.md](gmf.md#map-positions-in-the-game)). Note the recording
time with every reading. Open COMMUNICATIONS whenever its counter changes and
never delete a message unread. **[shot]** = a screenshot is enough;
**[rec]** = needs a recording (timing, motion or sound).

## 1. Game-wide mechanics

- **Compass and row 0** [shot]: at any start, point the camera along the
  compass's north and read the Tricorder at two points along that line;
  note which way row numbers grow. ([gmf.md](gmf.md#map-positions-in-the-game))
- **Starting colony from above** [shot]: in several missions per side (F1,
  K1, R1 at least), raise the camera straight over the start; screenshot the
  layout and read the Tricorder at each corner of the concrete pad and over
  each building. ([colony.md](colony.md#reluvas-layout))
- **Colony site positions** [shot]: at the start of each mission, hover the
  ground beside the hub and note the readout. Not known at all for F2, F3,
  F8, F14, J1F, J3F, J4F, J6F, J7F, J8F, K8, J6K, J7K, J8K, R1, R8, J8R, J9R,
  C11; the rest are estimated from mines and need a check (J6R thought row
  11-13, col 24-25). ([colony.md](colony.md#where-the-colonies-are))
- **Other sites** [shot]: read the Tricorder over the places messages name:
  K1 Taubat temple and the four groups of ruins (R1 too), R9 Federation and
  Klingon bases, J4R Klingon arsenal, C11 Hubrin fortress, J2 transporter
  (F/R read <112:280>, K <112:260>). ([play-r1.md](play-r1.md),
  [play-r9.md](play-r9.md), [play-j4r.md](play-j4r.md),
  [play-c11.md](play-c11.md), [play-j2r.md](play-j2r.md))
- **Tricorder mineral rule** [shot]: over one kind-1 (dilithium) block,
  hover the main view at high and at ground-level camera, then the relief
  inset; note when the ore name appears instead of plain terrain. Repeat on
  a kind-2 block (talgonite, J3R) and a kind-5 block with the whole readout
  in frame (magnesite: C11, J6R <200:253>, <212:254>, R9).
  ([play-j9r.md](play-j9r.md), [play-c11.md](play-c11.md), [gmf.md](gmf.md#resource-deposits))
- **Ores to materials** [rec]: watch a resource processor's stock and the
  six materials as each ore comes in; which ore makes which material, and
  what mined talgonite adds. ([gmf.md](gmf.md#open-questions), [play-j3r.md](play-j3r.md))
- **Mining rates** [rec]: one mining station on a fresh deposit; note its
  raw stock every minute and when it reports exhausted. ([play-tutorials.md](play-tutorials.md#open-questions))
- **Build times** [rec]: note order and finish times for each building,
  upgrade and vehicle on one side (ideally all three); include R9's device.
  ([play-r9.md](play-r9.md))
- **Building on deposits** [shot]: pick a power generator and hold its
  hologram over a deposit block (one the Tricorder names an ore on), then
  over bare ground beside it; is it red on the deposit? Reluva keeps
  deposits for mines only. ([game.md](game.md#the-colony))
- **Build costs** [shot]: hover every build icon on each side and screenshot
  its cost; missing even for the Federation: phaser tank, photon artillery,
  science block, phaser coil lab. ([play-tutorials.md](play-tutorials.md#build-costs-shown-structures-from-a-construction-yard))
- **Unit speeds** [rec]: send each vehicle type a known distance over flat
  ground (read the Tricorder at both ends) and time it.
  ([play-f1.md](play-f1.md#still-to-look-at))
- **Power** [shot]: COLONY DETAILS and a generator's panel before and after
  each generator and advanced generator; how many C6 needs.
  ([play-tutorials.md](play-tutorials.md#open-questions))
- **Colony capacity growth** [rec]: note the colony size maximum after each
  building finishes; which building gives which step (seen: 50, 55, 60, 75,
  80, 90 in R1, J4R, J6R, R14, C11) and how size can exceed it (R14 91/90).
  ([play-r1.md](play-r1.md), [play-j6r.md](play-j6r.md), [play-r14.md](play-r14.md))
- **Starting stock and size** [shot]: the NEW WORLDS panel in the first
  seconds of each mission; doubtful ones: J4R (small digits), J7 (38/90 vs
  45/90 on the Klingon side), J8 all three sides (45/50), R12 (very large
  stock, 50/90). ([play.md](play.md#what-they-established))
- **Camera** [rec]: lowest and highest heights, tilt and rotate keys (C1's
  tilt message), whether the view follows a selection.
  ([play-tutorials.md](play-tutorials.md#open-questions))
- **Communications panel** [shot]: can it be dragged to the top centre
  (seen in R9, R12, C11)? ([play.md](play.md#what-they-established))
- **Shield cover** [rec]: what turns a building's shield cover from BASIC to
  HIGH (J9R between 7:49 and 11:03). ([play-j9r.md](play-j9r.md))

## 2. Buildings and units (model matching)

Select each item and screenshot it close up, from two sides, with the
selection panel naming it ([colony.md](colony.md#buildings-and-models)).

- **Federation** [shot]: power generator, advanced generator, mining
  station (close), photon turret, defence artillery, shield generator,
  landing pad, food storage. ([play-f1.md](play-f1.md#buildings-and-their-models))
- **Romulan buildings** [shot]: most are matched now by their panel
  portraits ([colony.md](colony.md#romulan-matches)); still to confirm close
  up: the order of the two hub upgrades (`rch_b` tower first, `rch_c`
  spiked plate second?), the utility hospital upgrade (`ruh_b`?), the
  transporter pad (`rbcs`), shield generator (`rcss`), static disruptor
  (`rdds`) and photon defense (`rpds`) on the ground rather than by portrait,
  and the security centre (`risb`, never selected; is it J7R's tall finned
  tower?). ([play-r1.md](play-r1.md), [play-r2.md](play-r2.md),
  [play-j7r.md](play-j7r.md))
- **Romulan units** [shot]: the cloaked assault vehicle (full name, and
  whether it is `rapc` drawn cloaked), the surveillance squad (same portrait
  as the ground scout: the same craft?), the six starting units of J4R
  (forked icons: tanks?). ([play-j6r.md](play-j6r.md), [play-j4r.md](play-j4r.md))
- **Natives** [shot]: Metar temple and starship (`mss`); the Taubat phaser
  turret (`tsg`?); the Hubrin guide (`h`?) and the magenta map marker
  (J7R); Orion units (C11; `odv`?); what R2's black many-legged unit at
  36:30 is (only pale `ta` artillery craft and their dark shadows found).
  Settled: Metar colony core and spires `mch`, active portal `mport`, the
  grey slashed pods `th`, Taubat hangar `tw`, Taubat temple `tt`, R9's
  device `mad` (likely). ([play-r12.md](play-r12.md), [play-r14.md](play-r14.md), [play-j7r.md](play-j7r.md), [play-c11.md](play-c11.md))

## 3. Interface

- **Options and resolution** [shot]: open Options from the main menu;
  screenshot every page; note the in-game resolution and whether it matches
  the game.ini 640x480. ([play-tutorials.md](play-tutorials.md#open-questions), [level.md](level.md#gameini))
- **Difficulty and Cadet mode** [rec]: start the same mission at each
  difficulty and with Cadet mode; note starting stock, size and enemy
  strength. ([play-tutorials.md](play-tutorials.md#main-menu-and-setup-screens))
- **Panels never opened** [shot]: About, Multiplayer setup, and in play the
  COMMAND objective list at start, midway and end (K1, R1, C11), plus each
  MISSION REPORT page. ([play-k1.md](play-k1.md#still-to-look-at), [play-r1.md](play-r1.md))
- **Portraits per officer** [shot]: open STAFF on each side's hub and
  screenshot every officer card (face, name, section, post); then note each
  message's face and section. Settles which face goes with which section,
  and the Romulans' doubles (J1R, J2R, J3R, J7R, J8R, J9R, R9, R12, R14).
  ([gui.md](gui.md), [play-j2r.md](play-j2r.md))
- **Bonus loading box** [shot]: does C9's and C10's loading box name a bonus
  mission like C11's? ([play-c11.md](play-c11.md))
- **The star in star-system views** [shot]: in each briefing's star-system
  view, which body is the star and which is the mission's planet (J1R, J2R,
  J3R, J7R, J8R, R2, R9, R12). ([play-j1r.md](play-j1r.md), [play-r12.md](play-r12.md))

## 4. Per mission

Record every briefing in full. A "clip" is the briefing's voice clip; note
which page and view are on screen when it starts.
([media.md](media.md#open-questions), `src/engines/newworlds/briefing.cpp`)

**Federation**

- **F1** [rec]: messages 5-6, 14, 16, 23-24; the length of the Taubat deadline (inferred timer). ([play-f1.md](play-f1.md#still-to-look-at))
- **F3** [rec]: how the Romulan saboteurs objective is met (inferred).
- **F8** [rec]: clip `F8005`'s page. ([play-f8.md](play-f8.md))
- **F12** [rec]: clips `F12005` and `F12009`. ([play-f12.md](play-f12.md))
- **F14** [rec]: clip `F14013`. ([play-f14.md](play-f14.md))
- **J1F** [rec]: clip `J1F010`; the colony-size and radiation-scan objectives (both inferred). ([play-j1f.md](play-j1f.md))
- **J2F** [rec]: play to the end (recording stopped before the last objective); re-read the mine at 196, 436. ([play-j2r.md](play-j2r.md))
- **J4F** [rec]: clip `J4F006`. ([play-j4f.md](play-j4f.md))
- **J6F** [rec]: clips `J6F013`, `J6F014`. ([play-j6f.md](play-j6f.md))
- **J7F** [rec]: clips `J7F005`, `006`, `009`, `014`; when the Hubrin guide counts as arrived. ([play-j7f.md](play-j7f.md))
- **J8F** [rec]: the whole mission (recording ended minutes in); the anomaly device research step. ([play-j8f.md](play-j8f.md))
- **C9** [rec]: its map (forest: `f3` or `j3`; read the hub position); how the Orion objective ends. ([play-c9.md](play-c9.md))

**Klingon**

- **K1** [rec]: messages 3-6, 8-9, 12, 15-17, 19, 24, 27; COMMAND panel and mission report. ([play-k1.md](play-k1.md#still-to-look-at))
- **K8** [rec]: clips `K8006`-`K8009` (and the empty heading pages 7-9); all eight messages (seven unseen); how the Metar objective is met. ([play-k8.md](play-k8.md))
- **K14** [rec]: clips `K14007`, `K14009`; the messages beyond the four seen. ([play-k14.md](play-k14.md))
- **J2K** [rec]: play to the end; is the tenth message (new orders) on this side too? ([play-j2r.md](play-j2r.md))
- **J7K** [rec]: when the Hubrin guide counts as arrived; starting size. ([play-j7r.md](play-j7r.md))
- **J8K** [shot]: starting stock. ([play-j8r.md](play-j8r.md))
- **C10** [shot]: confirm `f2`'s map (hub readout). ([play-c10.md](play-c10.md))

**Romulan**

- **R1** [rec]: messages 5, 6, 12, 18; does the objective page show during clip `R1007`; are mission pass/fail pages and the planet stardate line shown; the alien-building scan (inferred). ([play-r1.md](play-r1.md))
- **R2** [rec]: messages 3, 7, 8, 13, 23; the opening colony's buildings; is the rescue-prisoners objective listed or tracked; what triggers the Orion taunt (scripted as a 4-minute timer); receiving page and mission-pass page. ([play-r2.md](play-r2.md))
- **J1R** [rec]: does the briefing end on signing on or after the last clip (pages 10-17); the reactor repair (scripted as an 80 s wait); the two messages near 29:07 and 33:00 with their numbers; does the APC capture option give the 30 % capture. ([play-j1r.md](play-j1r.md))
- **J2R** [rec]: the tenth message and whether it's won; who destroys the Klingon hub; the 5:43 counter. ([play-j2r.md](play-j2r.md))
- **J3R** [rec]: clip `J3R005` (galaxy step); message 7 (36:47-37:22); the ADDITIONAL page and objective 13; the spire in the weapon step; the active portal. ([play-j3r.md](play-j3r.md))
- **J4R** [rec]: does the second relief step play `J4R011` again and where does `J4R014` go; MISSION PARAMETERS page; messages before 21:03; were the Klingons still in, and does the security-center failure apply. ([play-j4r.md](play-j4r.md))
- **R9** [rec]: are `R9013`/`R9014` two steps (change near 3:11); the briefing's device and the red-white vehicle; how the science-block failure works (scripted as its destruction); does the device's blast destroy the Klingon colony. ([play-r9.md](play-r9.md))
- **J6R** [rec]: STELLAR CARTOGRAPHY page and clip `J6R005`; messages 4, 5, 9; the ground views and the council chamber; what destroys the Klingon hub and why that failure doesn't apply; the Federation-failed message. ([play-j6r.md](play-j6r.md))
- **J7R** [rec]: why backdrops lag the pages; message 5 (24:14-24:32, the transalumin quota?); first mine 188 or 180, 324; who destroys the Federation hub; the grey-blue crystal units and red-white fliers. ([play-j7r.md](play-j7r.md))
- **R12** [rec]: clip `R12005`; any step after page 6; messages 3/4 order and 9 (64:45-66:20); what ends the mission at 66:20; who kills the Hubrin observer. ([play-r12.md](play-r12.md))
- **J8R** [rec]: the white-and-red fliers and the big grey flier; the cratered pad; the device in flight and where it goes off; the mission report; polycomp after 5:29. ([play-j8r.md](play-j8r.md))
- **R14** [rec]: is the final Intelligence message the pass page; the mission report; who flies the white-and-red craft from 7:00; the briefing model under page 4 (pillar or spire). ([play-r14.md](play-r14.md))
- **J9R** [rec]: page 6's model; the captured starship; the octagonal green-topped tower; the ring at 5:18; duranium at 2:46, thermoconc at 12:40; the Metar temple failure (inferred); a campaign-end screen after the report. ([play-j9r.md](play-j9r.md))
- **C11** [rec]: message 13 (16:46-18:30); is there a time limit; which ambassador beams up and when each ambassador objective ticks; when the man counts as located; the Orion/Taubat structures message; the Hubrin fortress objective (scripted as met when it falls); the grey spires; the 75 capacity step. ([play-c11.md](play-c11.md))

**Tutorials** ([play-tutorials.md](play-tutorials.md#open-questions))

- **C1** [rec]: play to the end (completed or quit? report?); are the objectives (ids 10-16) shown; the tilt-keys message's clip; hub readout (272 or 373 in the second number).
- **C4-C8** [rec]: which clip plays with each in-game message; listen for `tut-2_05`-`07`, `12`, `17`, `22`, `27`, `tutor40`, `tutor41`.
- **C6** [rec]: does it complete, with what message; generator count and figures.
- **C7, C8** [shot]: briefing views for C7 page 49 and C8 page 65.
- **C1 briefing** [shot]: the green ship under `Tutor07`.
- **Map** [shot]: Tricorder readings at the map's far corners in C1 and C5 (size past 64 blocks; same map for C1 and C5-C8?).

## 5. Missing data files

- **Tutorial maps** [shot]: `c1.gmf`-`c8.gmf` are in none of the 24
  archives; ask which archive or CD folder holds the tutorial maps (search
  the CD and game folder for `*.gmf` and any extra `.cat`).
  ([level.md](level.md#in-reluva), [play-tutorials.md](play-tutorials.md#map))
- **Last joint clips** [rec]: `Media/RomVO` holds 113 files (confirmed), so
  J1R, J2R, J8R and J9R simply have fewer clips than the other sides; just
  check their briefings end where the recordings do.
  ([media.md](media.md#voice-overs-fedvo-klingvo-romvo))
- **Other archives** [shot]: a full listing of the game folder and the CD
  (including `MOVIES`), for anything beyond install.md's list (head-to-head
  `d1`-`d6` maps). ([install.md](install.md), [level.md](level.md))

## 6. R8 (no recording)

- **R8** [rec]: record the whole mission, the seventh Romulan one (after
  J4R, on `icewrld`'s `r8` map): loading box, the full briefing (clips
  `R8005`-`R8014`, page and view per clip), the starting colony from above
  with its Tricorder position, starting stock and size, every message
  (open each), selections of new buildings and units, the COMMAND panel,
  and the end through the mission report. ([play-r9.md](play-r9.md),
  [media.md](media.md))
