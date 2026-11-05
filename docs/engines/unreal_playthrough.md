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
- **The Ke'el Tul flyby** (CM01a_SW, about 18 s): the ship is lit in the
  recording and dark in Reluva.
- **Dialogue in play** (hails, Ops and Promenade talks, chatter): the
  recording's speech recognised and matched against the dialogue tables
  (689 lines found) depends on how it was played, so it can't be compared
  line for line. The type counts are kept with the recording's notes:
  battle chatter 187, cut scenes 173, talk to the player 162. No
  ammunition hails or "Energizing" anywhere in it.
