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
  22 s on matches. The docked Defiant under the arm is a brush mover
  (`Mover10`, Tag `MoveDefiant`: 562 polygons, 5000 across, its textures
  the ship's), not the `DS9_Defiant` actor. It was drawn black: a mover
  is lit by six lights chosen from its pivot, and on so large a brush six
  tiny red lamps by the pivot (radius 50 to 100) won over the station's
  lights of 4000. A light smaller than the brush now counts for the share
  of it it reaches, and a large brush (over 1000 across) with more lights
  on it than the renderer takes is lit from all of them, as worked out
  when the level was read, while it stays where it was. It now lies under
  the arm, lit, as in the recording (a little darker, as everything is).

- **The Ke'el Tul flyby** (CM01a_SW, 14-17 s in; the recording's 227-230
  s): the ship is now lit as in the recording, the same tan, about 1.35
  times darker, as everything is (below). It came out dark before the
  actor lighting took its lights by their full distance (Render.dll's
  rule, above). The glow behind it was white in Reluva, and a softer
  green-yellow in the recording. It isn't a corona (the one corona light
  there, Light39, is hidden behind the BSP). It is a steam jet's puffs
  (`DS9_SteamJet0`, smoke sprites, translucent), and their
  `ColorFilter` (the jet's `InitialColor`, 0.50, 0.65, 0.25) tints them.
  Reluva drew sprites without their `ColorFilter`, so the puffs, piled
  up, came out white. Render.dll's `DrawActorSprite` takes the filter
  times the glow (actors.md). Now the glow is green-yellow, as in the
  recording.
  - Since the steam's physics was read from Engine.dll (puffs at full
    glow until their last half second), Reluva's glow is hotter at the
    middle than the recording's: across it, green 255 over about 100
    pixels, falling to half by 50 to each side, where the recording's is
    a flat 200 to 240 over about 250. The puffs are made by the jet's own
    scripts (`CTVE_Generator.Generate`, `DS9_SteamJet.SetUpMultiMesh`:
    12 alive, growing 4 a second, `MaxScaleGlow` 1) and moved as the
    engine does, and Render.dll adds translucent sprites to what's behind
    (`DrawActorSprite`, its colour `ColorFilter` x glow). Twelve puffs
    of the smoke texture (at most 91 grey) added up saturate the middle;
    the recording's flat top looks like a renderer that doesn't add them
    up so, as its other differences (the transporter skin) suggest. To
    settle with footage of the original's own renderer.
- **Across the levels after the lighting changes** (November 2026):
  every chapter's level was shot again 0.3, 2.3, 7.3 and 15.3 s after
  its start, and set against the shots from before and the recording.
  - Where the scenes match, the frames that changed most moved toward
    the recording:
    - M01_SiskoL1B: the big door, a mover, is now lit as there.
    - M07_SiskoL1B: brighter, closer to the recording.
    - M07_WorfL2A: a smaller corona; its mean is now 28 against the
      recording's 26 (it was 43).
  - The other large changes are frames where the recording shows
    something else: it arrives by travel, or plays a space scene first.
  - The scripts (`ue-run -all`) give 89 errors and 2,416 None accessed
    over all 74 maps, against 258 and 3,860 in October's run.
- **Again, after the normals, IK, actor lighting and sprite colour
  changes** (November 2026): the same 38 chapters' 152 frames against
  the pass above. They moved little: 0.3 of 255 on average, at most 2.2.
  The scripts give 7 errors and 1,342 None accessed over all 74 maps.
  - M03_SiskoL1A: Sisko's EVA suit after the beam-in is lit only red
    now; it had a warm light too. The strongest light by weight (warm,
    under the floor 325 below) is cut off by the floor and a wall, both
    plain solid brushes, and Render.dll's actor lighting line-checks
    such a light from it to the actor (`UModel::LineCheck`, no extent).
    The recording shows the suit warm-white from the front, red from
    behind. Looked at again: no light gives that. Nothing between the
    suit and the warm light is open (solid rock all the way), the zone
    has no ambient light, the cut scene's camera is no light (its
    distance fog effect with no `LightType`), and the transporter has
    none. The warm-white frames are the beam-in's last second, while
    the suit's skin fades in (`MultiTextureOneScaleGlow` 0.57 to 1): the
    recording's renderer draws that skin as if unlit, full bright, as it
    draws it solid from the start (the transporter skin, above). Once
    the beam-in is over both light the suit red.
  - M11_WorfL1: Worf's suit is green now, from the two green lights 250
    across beside him, both in plain view. The recording plays the Ops
    cut scene there, so it can't say.

- **Heads turning to the talker** (CM01a_SW, from 29.5 s): in the
  recording Sisko turns his head to Worf as he asks "Mister Worf, does
  that look like an accident to you?". In Reluva he faced ahead. The
  scripts' `Pawn.UpdateHeadAnimation` ran and turned the head
  (`C3DApplyWorldRotation('Bip01_Head', ...)`, about 55 degrees of yaw
  by 31 s), but the drawing left the turn out. A pose with only the
  scripts' bone turns counted as unadjusted (`Adjust::empty` didn't look
  at them), so it was drawn as its animation alone. Now it turns as in
  the recording, a little less far at 32 s as it eases round. The same
  had kept every character's head and spine turns out of the drawing
  where nothing else changed the pose.
  - Each head's place, which the scripts read to aim it, still held the
    turns from the tick before. Engine.dll works the pose out afresh
    from the animation each frame before `UpdateHeadAnimation`, so the
    script sees the head as unturned. Reluva now clears last tick's
    bone turns first.
  - Worf doesn't turn to O'Brien as he speaks (WRF143). No one has
    a listener set for that line: the engine sets one only for a line
    in a cut scene, read from Engine.dll. So each head keeps its last
    target until its own random timer (0.5 to 2.5 s) runs out, and
    which way it looks then varies from run to run. The recording's
    head turns in that stretch are one such run.
- **The sound** of the opening (CM01a_SW from Sisko's start, 40 s): its
  loudness each 50 ms against the recording's agrees best with Reluva
  0.15 s ahead. Over each 5 s, the two correlate at 0.46 to 0.62 (the
  music, the effects and the speech mixed together), and 0.17 over
  30-35 s. Reluva's mix is 1 to 5 dB louder.
  - Over 30-35 s the lines start where the recording's do. Its subtitles
    change at 27.05, 29.50, 32.75 and 37.25 s, Reluva's at 27.00, 29.50,
    32.73 and 37.27 s, and WRF143's voice starts at 32.75 s in both. What
    differs is the Defiant's telemetry blips
    (`DS9_Defiant.telemetry.Defy_blipB3`, played at random times) and
    the music under them. Nothing to change.
- **Not compared:** the AI's pathfinding in play, and dialogue chosen in
  play. They depend on how the recording was played, as above. Its
  scripted scenes (the cut scenes and level openings above) are what
  can be set against it, and those agree.

- **The player's back** in M03_SiskoL2 (Sisko in the EVA suit, three
  lamps ahead of him) is now lit as in the recording; it was nearly
  black. Render.dll makes each vertex's normal from the triangles'
  winding alone: `(p1 - p0) x (p2 - p0)`, each made one long, summed and
  made one long again. Reluva had guessed which way was out from each
  mesh's middle on its first frame, and for the EVA suit the guess
  turned every normal inward, lighting its front from the lamps behind
  it. The same suit's back in M01_SiskoL1B is lighter too; M03_SiskoL1B,
  M06_SiskoL1C, M07_SiskoL2 and M07_SiskoL3A are as they were.
## Open

- **Overall brightness**: the recording runs brighter than Reluva by 1.2
  to 2.3 times in mean level, varying by scene (a brightness setting, its
  renderer, or both). The first recording matched within 10-20%, so
  nothing was changed for this one.
- **The transporter's look.** In the recording the beamed-in body shows
  its own lit skin under the sparkle from the start; in Reluva it shows
  only the sparkle until the crossfade (2.2 s in). The script draws the
  skin in `STY_Blended` with `MultiTextureOneScaleGlow` 0 and raises it to
  1 over 2.5 s. Render.dll hands that glow to the device's
  `SetBlendRatio`, and both shipped drivers draw a blended polygon with
  its light and its alpha times that ratio (D3DDrv's
  `SRCALPHA`/`INVSRCALPHA`, OpenGLDrv's `GL_SRC_ALPHA`), so by the code
  it starts unseen, as Reluva draws it. Read again in full (November
  2026):
  - **The passes:** with `bMultiTextureOn` there's no other skin pass.
    The first pass is the skin itself, restyled by
    `MultiTextureOneStyle`; the second is the sparkle.
  - **The fallback:** a device that refused `SetBlendRatio` would get the
    pass translucent at a hundredth of its light. But all five shipped
    drivers accept it (D3D, OpenGL, Glide, MeTaL, software).
  - **The colour:** the drivers scale a blended polygon's colour by the
    ratio as well as using it as alpha. The skin fades in as glow²,
    darker halfway than Reluva had it (now as they do).
  No path in the game's own code shows the skin at glow 0. The
  recording's renderer seemingly draws it opaque, as it is also brighter
  throughout. Its
  column is also lower and shorter (at most knee height, gone 0.5 s
  sooner); Reluva's spans the body, as the script's scale (twice the
  height over 80) and the quad mesh centred on its pivot (Z -200 to 200)
  give. Render.dll has been read on it. Each MultiMesh copy's Location,
  Rotation, AxisScale, DrawScale, Texture and glow stand in for the
  actor's for one draw (`DrawActorSpriteMM`). The mesh is then placed as
  any other: AxisScale times the mesh's Scale, about its pivot. There's
  no case of its own for the height. So by the code the column runs from
  the feet to 20 over the head as it appears, as Reluva draws it. As it
  goes, its top falls to the feet, which would be knee high only near
  the end. Still to settle from closer footage: whether the recording's
  column is that falling end, or the beam texture (`FedTransBeam01`)
  shows mostly near its bottom.
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
