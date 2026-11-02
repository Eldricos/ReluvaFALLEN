# The Fallen: saved games (.dsa)

A saved game of the original's is the level itself, written out as an
ordinary package (version 73) by the engine's `SaveGame`: everything the
level's `Level` object leads to, its BSP and the movers' brushes
included. It imports only the game's shared packages and never opens the
map's `.dsm`. Editor-only objects (brushes, UnrealEd's cameras, text
buffers) aren't in it. `reluva-tool ue-save <file>` reads one.

## Names

`SaveGame QuickSave` (F6) writes `QuickSave.dsa`. Saving from the menu
names the file from the level's `Title` (else its `LevelName`), spaces as
underscores (64 letters at most), then a dash and the seconds the player
has played (`GamePlayedTime`), counting up past a file already there:
`Bajoran_Ship_-_Ke'el_Tul-97.dsa`. They go in `..\Save` (Default.ini's
`SavePath`). The game's load list shows every `.dsa` there, named by its
file and dated by it.

## What's in it

- **The actors**, as they were. Each that has a state starts with its
  state frame (the object flag `HasStack`, 0x02000000): its code's node
  and its state (object references), the events it ignores (8 bytes), its
  latent action (an i32) and, when the node isn't none, where in the
  node's code it is (a compact index; -1 none). Then its properties, and
  nothing after. The player's `Player` is none (the viewport isn't saved).
- **LevelInfo** with `bBegunPlay` set, `Pauser` "Player" (the game pauses
  to save), its `TimeSeconds` and `NoPauseTimeSeconds`.
- **The Level object**, as a map's, then The Fallen's additions:
  - its properties (none); the actor list (a count, the room, the
    references; the second always none); the URL (protocol, host, map,
    portal, options, port, valid); the model; the reach list; the level's
    time when written (f32); the first deleted actor; 16 text blocks; the
    travel information (string pairs);
  - **the ghosts**: a count and the references of the actors set aside
    (`MakeGhost`), which aren't in the actor list;
  - **the dialogue engine**: an i32 (set up); its missions, line types
    and speakers (each a u16 id and a string); its lines (each seven u16:
    one unknown, the type's priority, **how often it's been said**, its
    number, one unknown, its next and return line or 0xFFFF; its voice,
    key, text; an f32 pause; its NEXTLINE and RETURNLINE patterns; its tag
    (a name); its event and condition; a name, a u16 camera and four
    names); two lists of what's under way (records of u16, u16, i32,
    reference, i32; and u16, reference, i32); its faces (as the name
    tables); its cameras (13 i32 each); an i32;
  - **the sounds playing**: for each, the actor, the audio driver's id
    (actor x 16 + slot x 2 + no-override; below 0 slot none), the sound's
    path, its volume, radius and pitch (f32), and when it began (level
    seconds);
  - an i32 (0).
- **The C3D mesh states** (`C3DMESHSTATE`, one an actor with a C3D mesh,
  its `C3DOurMeshState`): no properties; 0xFFFFDE80, 2, 22 (the C3D
  library's version); the mesh's name; an i32; the IK targets; an i32;
  then the animation groups (FullBody, LowerTorso, UpperTorso, then the
  face's channels sk3...): each its name and sequence (`NONACTIVE` for
  none), an i32 source, the frame (f32), the type (1 playing, 2, 4 a face
  channel), the state (a face channel's 5 while tweening), rate, weight,
  the tween's from-frame and from-weight, elapsed, an f32, its length, an
  f32, a pending tween (a sequence, frame, weight and time) and a string
  (the bone a layer starts at) and three i32. Then the bones' adjustments,
  what's attached, the owner's name. Its strings are a 16-bit length and
  the letters; its arrays a count, the count again and a growth step.
- **CTVE_MultiMesh** objects: the effects' particle pools (2,285 in the
  example), each in its effect actor.

Checked on `Bajoran_Ship_-_Ke'el_Tul-97.dsa` (M01_SiskoL1A, 97 seconds
in): 1,857 actors, 33 ghosts, 977 dialogue lines (6 said), 2 sounds
playing (the music, the HUD's confirm click), 118 mesh states, all read
to their ends.
