# The Fallen against a full playthrough

A recording of the whole game (all three characters, 9 h 27 min, 1280x720
at 60 frames a second, with chapters by level) was compared with Reluva
(November 2026). Each level load in it was found where the frame stops
changing for over 0.6 s (the LOADING panel); that is the level's time 0.
Reluva's frames were then taken at the same times after its start (0.3,
2.3, 7.3 and 15.3 s). Many chapters begin with an interlude map's cut scene
rather than the level the chapter is named after: a new game, for example,
starts on `CM01a_SW/CM01_A_Sisko01`, the space flyby and bridge scene,
before M01_SiskoL1A.

The recording is from a later build than these files. Its end credits have
a "Thanks to" block (Dewanda Howard, Kim Kindya, Michael Petersen, Diane
Strack) before PARAMOUNT that `DS9_Credits` here doesn't have. Where it
differs only in that way, Reluva follows its own scripts.

## Matching

- **End credits** (`CM_SpaceStation/Endcredits`): the start's
  `StartingDirector` IntroFlyby2 path, the text's positions (fractions of
  the screen: 0.055 a name, 0.075 a heading) and its scroll (0.1 of the
  screen a second) match. The extra block above accounts for the 3 s
  Reluva runs ahead by the end.
- **Cut-scene dialogue timing**: in CM01a_SW Reluva begins the log at
  2.07 s, the distress call at 5.83 s and Worf's "The Bajoran vessel has
  sustained heavy damage" at 21.53 s. The recording shows the same
  subtitles by 2, 7 and 22 s after its load. `ue-play ... lines` now gives
  each line's start in the level's seconds.
- **Subtitles and letterbox**: the original's are the HUD scripts' own
  (`CTV_Hud.RenderLetterBox`, bars of `LetterBoxMarginTop`/`Bottom` 0.10;
  `DS9_Hud.DrawSubtitleText` at 0.92 down, the speaker's name in the dark
  font, the text in the light one, typed out at 150 letters a second).
  Subtitles are off by default (`DefUser.ini`: `bDisplaySubtitles=False`);
  the recording had them on. Reluva's viewer used to paint its own black
  bars over the whole screen in cut scenes, covering the subtitles; it no
  longer does, and they now draw as in the recording. The one difference:
  Reluva draws the talker's portrait left of the name, as these scripts'
  `DisplayMugShot` does in a letterbox; the recording's build draws none.
- **LOADING panel**: `CTVE_EndLevel` calls the HUD's `ShowLoadScreen`,
  and `RenderFadingLoad` draws `DrawLevelAction(LoadingString)` over the
  last frame, centred. That is script, run as it is.
- **The HUD's weapon panel** at a level's start: a fresh start holsters
  the weapon (`EquipInventory` ends with `DoHolsterWeapon`), so the panel
  shows the hand. The recording arrives by travel with the phaser drawn.
- **The chase camera**: `CTVE_Camera.CameraAI`'s `FocusPitch` -0.1 gives
  -18 degrees, as in the recording.

- **Opening cut scenes**: `CM01a_SW` (the flyby and the bridge), from
  both `CM01_A_Sisko01` and `CM01_A_Worf01`, cuts between cameras at the
  same moments as the recording: all 17 cuts in the first 90 s within
  0.3 s (Reluva sampled every 0.5 s; the cuts found where the picture
  changes at once). It ends 105.5 s in, and the recording's next load
  starts 105.6 s after its own. M01_SiskoL1A's opening (the shuttle bay)
  then matches frame for frame, and M03_SiskoL1A's descending camera
  (`CM03-SiskoL1A`, Director8 to 6 to 7) matches each recorded frame
  to within 0.3 s throughout. `ue-play`'s `shot` now gives the level
  time it was taken at (each shot plays a frame of its own, 33 ms).
- **The framing**: the recording keeps the 4:3 picture's height and
  shows more at the sides. Its middle 4:3 lines up with Reluva's whole
  frame.
- **The transporter's timing** (`CTV_TransporterBeamIn`, M03_SiskoL1A
  10 s in): the column appears, the sparkle spreads over the body and
  then crossfades into the skin at the same times as in the recording.

- **Sparks**: a phaser's hit throws 2 sparks a frame along its
  reflection, falling under gravity (-450) and gone in 0.4-1.2 s
  (`DS9_BeamWeaponFX`); Reluva's fall and fade so. The recording's cloud
  is denser because the recording runs at more frames a second.
- **The capture on SR-III** (`CM06_SW_Capture`, from 7688.6 s): its cuts
  agree (6.5 and 13.3 s). Sisko (CIN_Sisko, facing the camera) was drawn
  black: the scene's only lights are three sky cylinder lights 11000
  units above (radius 6400), chosen by their distance across but then
  shaded by their full distance, which is past their radius. A cylinder
  light now shades an actor from the actor's own height; he and the
  Jem'Hadar are lit as in the recording. Hass'terral's snowfield, lit the
  same way, looks as it did.
- **Arduria** (`CM_ArdPrison/CM07_A_Sisko01`, from 7719.75 s): the camera
  matches. Each zone now shows its own sky, as `ZoneInfo.LinkToSkybox`
  links it (the SkyZoneInfo tagged the zone's Event, else the first);
  Reluva used to show the level's first everywhere. This map has two:
  `CM07A` (a sun) and `CM07B` (a moon); the opening's zone is `CM07A`'s,
  and now shows the sun, as in the recording. The prison overhead from
  22 s on matches.

## Open

- **Overall brightness**: the recording runs brighter than Reluva by 1.2
  to 2.3 times in mean level, varying by scene (a brightness setting, its
  renderer, or both). The first recording matched within 10-20%, so
  nothing was changed for this one.
- **The player's back** in M03_SiskoL2 (Sisko in the EVA suit, three
  lamps ahead of him) is bright in the recording and nearly black in
  Reluva. The actor lighting (Render.dll's, read again: (cos + 1)^2 - 1.5,
  linear falloff, zone ambient plus AmbientGlow, up to three static
  lights) gives nothing to faces turned from the lights. Reversing the
  meshes' normals matches this scene but changes little in the other
  levels checked (M03_SiskoL1B, M06_SiskoL1C, M07_SiskoL2, M07_SiskoL3A,
  the CM01a_SW bridge). Until Render.dll's mesh normals are read, it stays
  as it is.
- **The transporter's look.** In the recording the beamed-in body shows
  its own lit skin under the sparkle from the start; in Reluva it shows
  only the sparkle until the crossfade (2.2 s in). The script draws the
  skin in `STY_Blended` with `MultiTextureOneScaleGlow` 0 and raises it to
  1 over 2.5 s. Render.dll hands that glow to the device's
  `SetBlendRatio`, and both shipped drivers draw a blended polygon with
  its light and its alpha times that ratio (D3DDrv's
  `SRCALPHA`/`INVSRCALPHA`, OpenGLDrv's `GL_SRC_ALPHA`), so by the code
  it starts unseen, as Reluva draws it. The recording's renderer
  seemingly draws it opaque, as it is also brighter throughout. Its
  column is also lower and shorter (at most knee height, gone 0.5 s
  sooner); Reluva's spans the body, as the script's scale (twice the
  height over 80) and the quad mesh centred on its pivot (Z -200 to 200)
  give. Not yet explained.
- **The Ke'el Tul flyby** (CM01a_SW, about 18 s): the ship is lit in the
  recording and dark in Reluva.
- **Arduria's docked Defiant**: in the recording the Defiant lies under
  the docking arm for the first 7 s; Reluva shows the arm without it. The
  `DS9_Defiant` actor is drawn, but far off at its own place, and
  `Mover10` (Tag `MoveDefiant`, a 190 KB brush in front of the camera)
  shows nothing there. Not found yet.
- **The swarms**: SR-III's firefly swarms (`SR3_FireFlySwarm`, Size 0.05)
  are too small to see in the opening scenes, in either; Jeraddo's bugs
  appear only in play. Not compared.
- **Fire and water speed**: no fixed view of a fire or water texture
  found in the recording yet. M05_SiskoL1a's burning pod is brighter in
  the recording (as everything is), its motion not measured.
- **Dialogue in play** (hails, Ops and Promenade talks, chatter): the
  recording's speech recognised and matched against the dialogue tables
  (689 lines found) depends on how it was played, so it can't be compared
  line for line. The type counts are kept with the recording's notes:
  battle chatter 187, cut scenes 173, talk to the player 162. No
  ammunition hails or "Energizing" anywhere in it.
