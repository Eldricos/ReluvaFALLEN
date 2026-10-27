# The UnrealScript virtual machine

Status: **runs The Fallen's levels with the player in them**: the
Maps page plays a level (Enter), its scripts live, the player moved by
the keyboard and mouse, seen from the game's own camera, the AI finding
its way along the level's paths and the characters' lines shown as
they're said. It isn't the game yet: nothing makes a sound, and the HUD
and menus aren't drawn. Every class of the game loads (`vm.cpp`), every default
property converts to its type, and all 74 maps start: their actors made,
the game type spawned, the start-up events sent, the player logged in as
the level's character, then twenty seconds of ticks, with no crash. The
script errors left are mistakes in the game's own scripts (below) and
None accessed where natives not written yet would have filled
something in. The Maps page draws each level as its scripts leave it
when play begins.

Clean room as the rest of the engine ([unreal.md](unreal.md)): the
bytecode and the objects are as [script.md](../formats/unreal/script.md)
reads them, what each native does comes from its declaration (name,
parameters, flags, as `reluva-tool ue-script` shows them) and from what
the scripts calling it expect, and the order of the start-up events is
Reluva's reading of the public UnrealScript documentation of actors'
lifetimes. None of it is translated from Epic's code or the clean-room
port's.

## Values and objects

Values are kept by property, not in raw memory as the original does: an
object holds one `Value` per property element (`VmObject::values`, its
class's slots: inherited properties first), a struct value one per
field. A value is a byte, int, bool, float, name (interned for the whole
VM, compared without case), string, object (classes are objects too:
`VmObject::asClass`) or struct. Objects the VM doesn't run (textures,
sounds, meshes) are stand-ins holding where they are in their package.

Classes load from their compiled script on first use (`Vm::classAt`):
their parent first, then their fields (properties, functions, states,
structs, enums), then their defaults over the parent's. A property a
subclass declares again (The Fallen redeclares `SoundVolume`, `SoundPitch`,
`SoundRadius`, `Acceleration` with other types) is found most derived
first. Struct values in default properties are stored as their fields
one after another, each as a property of its type is (bytes 1 byte, ints
and floats 4, names and objects compact indices, strings as strings,
bools 1 byte); every one of The Fallen's reads to its end. Objects' own
built-in properties (`Outer`, `Name`, `Class`) are set when they're made:
scripts read them (CTVE_DllObject checks its `Outer` is an actor).

An object as a string (`string(Level)`, `$`) is its path, as UnrealScript
gives it:
- an object from a package is `Package.Group.Name`;
- a class is `Package.Class`;
- a level's actor is the map's name and its own (`M01_KiraL1A.LevelInfo0`);
- anything else is its `Outer`'s path and its name.

The scripts depend on this: The Fallen's `GetMissionLevel` takes the
mission number from `string(Level)`, `GetItemName` strips the package off
a class's, and the mutator list compares against `Package.Class`. While
it gave the name alone, every level counted as mission 100.

## The interpreter

Statements run from a function's or state's code (`Interpreter::run`),
expressions evaluated recursively (`eval`), variables through references
(`ref`) so assignments, out parameters and `++` change them in place.
What it does:

- Variables: local (the call's frame), instance (the context object:
  `self`, or the left of a `.`), default (the class's defaults), array
  elements (bounds checked), struct members.
- Calls: virtual (the object's state's function first, its parents'
  states', then its class's and parents'), final (that function: `Super`
  calls and final functions), global (skipping states), native (by
  number, through the function that declares it), static (on a class).
  Arguments are converted to the parameters' types; out parameters are
  written back after; optional ones left out are empty. `&&` and `||`
  leave their right side unevaluated when the left decides.
- `None`: reading through it gives an empty value and is counted
  ("Accessed None"), as the original logs and carries on.
- Control: jumps, `switch`/`case`, `foreach` (the iterator native gives
  its results, the loop assigns each to its out parameter in turn,
  skipping actors destroyed meanwhile), `return`, `stop`, labels and
  `goto` in state code (a missing label is reported and the code carries
  on), `assert`.
- States: `GotoState` calls `EndState` in the old state and
  `BeginState` in the new, and sets its code to the label (`Begin` by
  default; `Auto` finds the class's auto state). A state inherits a
  parent class's same-named state's functions and code.
- State code runs each tick until it waits in a latent function
  (`Sleep`, `FinishAnim`, pawns' `MoveTo` and kin, and those not
  written yet, which wait a tick so loops round them don't spin) or
  stops. Each object's timer (`SetTimer`) and `Tick`
  run each tick; `Disable('Tick')` and the like stop the engine calling
  them.
- Guards: calls nested past 250, a function past 2,000,000 statements or
  a tick of state code past 20,000 are stopped and reported.

Natives are C++ functions registered by the qualified name of the
function declaring them (`Object.Add_IntInt`): all of Core's Object
(`vm_natives_core.cpp`: the operators for every type, maths, vectors and
rotators, strings, states, `IsA`, `DynamicLoadObject`...), and the
Engine ones a level needs so far (`vm_level.cpp`): `Spawn`, `Destroy`,
`SetTimer`, `Sleep`, location, rotation and collision setters, the actor
iterators (`AllActors`, `ChildActors`, `BasedActors`, `RadiusActors` and
the visible ones by radius, `RadiusPawns`, `AllPawns`), the pawn list, animation
groups from the actor's mesh. A native not written yet returns an empty
value and is counted.

## A level

`ScriptLevel` makes a map's actors from the level's actor list (the
Level object's: a count, then compact references; the first is the
LevelInfo), each with its class's defaults then its saved properties,
their `Level`, `XLevel` and `Outer` set. Then `beginPlay`:

1. The game type (the LevelInfo's `DefaultGameType`, else the .ini's
   `[Engine.Engine] DefaultGame`: `DS9.DS9_GameInfo`) is spawned and
   `InitGame` sent.
2. Every actor: `PreBeginPlay`, `BeginPlay`; then every actor
   `PostBeginPlay`; then every actor `SetInitialState` (which puts it in
   its initial or auto state). The LevelInfo's `bStartup` is true
   meanwhile, and `bBegunPlay` true after: The Fallen's splines do nothing
   on a tick before it is, so without it no opening sequence ever ends.
3. Spawned actors get `Spawned`, `PreBeginPlay`, `BeginPlay`,
   `PostBeginPlay`, `SetInitialState` as they're made; destroyed ones
   `Destroyed` (and their owner `LostChild`); `bStatic` and `bNoDelete`
   actors refuse to go.
4. Each tick an actor's `LifeSpan`, if it has one, counts down. At 0 it
   gets `Expired` and is destroyed. The Fallen's generators (sparks, the
   Jerrado bug swarm: one every 5 ms, each to live 0.4 s) count on it.
   Without it, M03_KiraL2's swarm had made 17,500 actors by 20 seconds
   in.

UnrealEd keeps its viewports' cameras in the level as `Camera` actors
(292 of them over The Fallen's maps). `Camera` is a `PlayerPawn`, and in
single player `GameInfo.Login` takes over any `PlayerPawn` in the level
that has no player (meant for players travelling between levels), so
they're left out; the original must drop them too, or every login would
become an editor camera.

### Where actors are

The engine keeps each actor's `Region` (its zone, BSP leaf and zone
number) as it's spawned and moves; scripts read it
(`Level.Player.Region.Zone.bWaterZone`). `ScriptLevel::regionAt` walks
the level's BSP from the root to the point, taking the zone and leaf on
its side of the last node. Against the regions saved with The Fallen's
placed actors (four maps, 2,808 actors with one): the same zone for all
but 8, the same leaf too for 2,738. Actors saved with no region (the
LevelInfo, brushes: zone 0, leaf -1) aren't in the world, and keep
none.

### The player logging in

After `beginPlay`, `login` does the engine's side of a player joining,
in this order (Reluva's reading of the public UnrealScript documentation
and of what the scripts expect):

1. The start: the game type's own `FindPlayerStart`.
2. The character: The Fallen's starts (`CTVE_PlayerStart`) name it in
   their `Player` property (M01_KiraL1A's: `PLYR.DS9_Kira`), which no
   script reads, so the game's engine must spawn it; otherwise the .ini's
   `[URL] Class` (`PLYR.DS9_Sisko`), else the game type's
   `DefaultPlayerClass`.
3. The game type's `Login` with options `?Name=Player?Class=...`: it
   spawns the pawn at the start (whose `PreBeginPlay` sets The Fallen's
   `Level.Player`).
4. The engine's own objects for it: a viewport (`Viewport` has no script
   class, it's C++ only: made up as a kind of `Player`, `Vm::defineIntrinsic`,
   so `Viewport(Player)` casts work) holding the .ini's console
   (`UMenu.DS9_Console`).
5. `Possess`, the game type's `AcceptInventory`, `TravelPostAccept`,
   the game type's `PostLogin`.

On M01_KiraL1A this plays the opening as the scripts have it: Kira's
`InitPlayerStart` gives her the start's weapons and items, spawns her
HUD (`DS9_Hud`), triggers the opening camera director, and freezes and
hides her for the start's 12 seconds; then `CTV_Human`'s `Frozen` timer
beams her in (`CTV_TransporterBeamIn`), which puts her in `PlayerWalking`
about 17 seconds in.

`reluva-tool ue-run <game> <map | -all> [seconds]` runs it and reports
what scripts did: their log (repeats counted), errors, natives not
written yet by calls, None accessed by place, and the player as it ends
up (its state and HUD), and how things moved. On The Fallen (October
2026), all 74 maps, 20 seconds each, in 6 minutes: 185 million calls. A
player logs in on every map, as twelve characters (DS9_Sisko and DS9_Worf
on 15 maps each, DS9_Kira, their EVA, ES, Beaten and Kira's Cardassian
disguise on the rest), all with their HUD but on Entry (the menu's level,
whose start isn't The Fallen's). 20 seconds in:
- 65 are walking.
- 4 are still frozen in their map's opening cut scene. Before animations
  kept time, 7 were: cut scenes waiting on `FinishAnim` now end when
  their animations do.
- 4 started in water and swim.
- On CM_Endcredits, the credits, the player falls: there's nothing under
  its start.

The movement: 187,000 moves, 6,837 landings, 8,341 touches.
- 9,908 placings refused: scripts putting things where there's no room,
  mostly SR3_Raptor and DS9_SmallFishSchool on M05_SiskoL1b, again and
  again at one spot.
- 367 walks to a goal given up: SR3_Leech mostly.

What's left before play, by what `ue-run` reports:

- **Natives not written** (20): The Fallen's `ListActors` iterator (an AI's list of known actors, not yet
  understood), `GetRendererFlags`, and The Fallen's per-tick hooks and
  effects (`ScriptTick`, `ChainSparkTick`, `UpdateBeamSections`...).
- **Speed**: values are copied freely and calls looked up by name; the
  biggest maps tick at half real time.
- **The game's own mistakes**: `DS9_GrigariSentryDrone` (and its kin)
  `goto 'DetermineAction'` in their `DefensiveStance` state, where it's
  a function, not a label, `DS9_Grigari` `goto 'CheckCloak'` in
  `MeleeAttack` and `DS9_FriendlyAI` `goto 'AlarmRunner'` in
  `Acquisition`, which have no such labels (reported, and the code
  carries on); `CTV_ElevatorMover.InitRamp` and `CTV_TriggerLight.Tick`
  divide by an elevator's move time or a light's change time, 0 in some
  (floats divide to infinity, as they do: the light's fade then ends at
  once; reported). `DS9_JerradoBugStream.SetUpMultiMesh` divides by its
  owner's speed, 0 when it stands still (each piece of the swarm made
  then: reported). `SR3_Raptor` `goto 'Wait'` in its `Dying` state,
  which has only `Begin` and `FinishedDeath` (a `GotoState('Wait')`
  meant, it may be; reported).

## Movement

Actors move through the level's BSP ([collision.md](../formats/unreal/collision.md))
and each other (`vm_physics.cpp`). This is Reluva's own model of UE1's
movement, built from what the scripts set and expect.

- **A move** (`Move`, and physics) sweeps the actor's box through the
  world, if it has `bCollideWorld`. Then it sweeps against the cylinders
  of actors that block it (`bBlockActors`, `bBlockPlayers` for players,
  not its owner or what it owns) and movers. A mover is collided with
  through its brush's own BSP (UnrealEd builds one for each mover), with
  the move turned into the brush's coordinates where the mover is now. A
  mover without one is a box. Traces count movers as the world, so a
  pawn stands on a lift. It stops a little before the first hit. After
  that come zone changes (`ActorLeaving` to the old zone, `ZoneChange`,
  `ActorEntered` to the new), what it now touches (`Touch` and `UnTouch`
  both ways, the `Touching` list), and `Bump` to both if an actor
  stopped it.
- **Smooth moves** (`MoveSmooth`, walking, swimming, flying): a wall
  that stops a walker is first tried as a step (up `MaxStepHeight`,
  across, down onto a floor). Otherwise it gets `HitWall` and the rest of
  the move slides along the wall, twice at most. Walkers don't climb
  walls by sliding.
- **Placing** (`SetLocation`, spawning): if the spot is solid (the
  world, or a mover that blocks), the actor is nudged up or sideways to
  the nearest open one, else refused. Starts are placed for the default
  pawn height (40), and The Fallen's characters are taller (Worf is 52).
  So a player starting on a lift begins a little into its floor and is
  lifted onto it.

Physics each tick, after the actor's scripts:

| Physics | What it does |
|---|---|
| Walking | Acceleration (flattened, at `AccelRate`) against ground friction (the zone's, 8) up to `GroundSpeed`. A smooth move, then down onto the floor if one is within a step, else falling (`Falling`). A floor is a surface facing up at least 0.7 (about 45 degrees). |
| Falling | Gravity (the zone's, −950) and `AirControl` of the acceleration, up to the zone's terminal velocity. Off a wall or a slope too steep to stand on, `HitWall` and the rest of the fall slides along it. On a floor, `Landed`, then walking (pawns) or none, keeping the speed across. As a fall starts, the actor's `FallHeight` is set where it is: The Fallen's `TakeFallingDamage` kills at a drop of more than 800 from it, and its scripts set it only while rising in a jump. Left at 0, every landing on a map far below the origin was fatal. |
| Swimming, flying | Acceleration against fluid friction up to `WaterSpeed` or `AirSpeed`, smooth moves. Swimmers sink or float by `Buoyancy` against `Mass` and fall once out of water. |
| Projectile | Its velocity plus acceleration; `HitWall` on the world. |
| Rotating | `RotationRate` each second. Any physics also turns toward `DesiredRotation` with `bRotateToDesired` (not a player's pawn: its input turns it). |
| Moving brush | While its script has it going (`InterpolateTo` sets `bInterpolating`): a mover from its old key toward its next by `PhysAlpha`, eased in and out if it glides by time. It carries the actors based on it and sends `InterpolateEnd` at the end. |

The Fallen's own physics modes (its effects', past 32) do nothing yet.
Natives: `Move`, `MoveSmooth`, `SetLocation`, `SetPhysics`, `Trace`
(the world and actors' cylinders), `FastTrace`, `PointCheck`, `Pawn`'s
latent `MoveTo`, `MoveToward`, `TurnTo`, `TurnToward`, `WaitForLanding`
and `FinishRotation` (each moves the pawn by its acceleration until it
gets there or gives up, then its state code carries on), and
`pointReachable` / `actorReachable` (a box swept there), and The
Fallen's `MoveTo2` (taken as `MoveTo`), `StrafeTo` and `StrafeFacing`
(there, facing a place or an actor all the way).

The loops over every actor (blocking, touching, what stands on a mover)
read each class's collision properties from where they are in its
values, found once; looking them up by name took a quarter of a busy
level's time.

### The AI's paths

`vm_paths.cpp` finds the AI its way across the level's navigation points
(path nodes, patrol points, cover spots...). This is Reluva's own
search over the paths the editor stored with the map.
- **The paths:** the Level object holds, after its actor list, its URL
  and its model, a reach list: for each reach, its length, its start and
  end points, the biggest collision radius and height that fit along
  it, how it's travelled (walking 1, flying 2, swimming 4, jumping 8,
  doors 16, special 32, the player only 64) and whether it was pruned.
  Each point's `Paths[16]` are the reaches from it. Checked: on every map
  the reaches resolve to the level's points and each starts at the point
  listing it; all of The Fallen's are walking ones (M05_KiraL1: 348
  points, 2293 reaches, 817 listed).
- **The networks are islands.** On M03_SiskoL1A the biggest set of
  points that reach each other is 33 of 79, and on M05_KiraL1 247 of
  348: rooms joined by doors and lifts, ledges with no way down (there
  are no jumps or drops among the reaches). Ladders are the player's
  alone.
- **`FindPathToward(Actor)`, `FindPathTo(Vector)`:** the goal itself if
  the pawn can go straight there (`pointReachable`'s sweep). Else the
  nearest few points (up to 8 within 1200) the pawn can go straight
  to, and those that can see the goal (or the goal, if it's a point).
  Then the shortest way between them (Dijkstra) along reaches the pawn
  fits and can take (`bCanWalk`, `bCanFly`... against the reach's
  flags), each costing its length and its end's `ExtraCost`. The route
  goes in the pawn's `RouteCache`, and the answer is its first point
  the pawn isn't already at; None if there's no way (the scripts then
  wait, or give up).
- **The iterators:** `AllPathNodes` (by class, and tag), `RadiusPathNodes`
  and `RadiusVisiblePathNodes` (those within a radius, and those of them
  that can see the place: nothing in the world in the way), and
  `LineOfSightTo` (from the eyes to the other's middle, or its top).
- **In play:** on M01_WorfL1 the Grigari now roam from point to point at
  a walk (they stood still before). Over all maps' first 20 seconds,
  about 15,000 ways were straight, 9,700 along paths and 15,000 none:
  mostly pawns asking again and again for a point in another island.
  `ue-run` reports these per map.

### Dialogue

`vm_dialog.cpp` plays the characters' lines from the game's dialogue
tables ([dialogue.md](../formats/unreal/dialogue.md)). The table format
is the game's, and so are the events it calls on the scripts' side. How
lines are picked and timed is Reluva's reading of how the scripts use
the natives.
- **Which lines are the level's:** those of its own table (LevelInfo's
  `DialogFile`) and the general one, or of every table if it names none.
  Of those, the ones for whoever the player plays: a K, S or W mission
  for Kira, Sisko or Worf (by the player's `Speaker_Tag`), G and M for
  anyone. The missions follow the story, not the maps (the Promenade
  opens with a mission 11 line), so the map's own mission number only
  makes a line preferred.
- **`FindBestDialogLine(out Index, Type, FilterTag, ListenerTag,
  SpeakerTag, LineID)`:** the lines of that type, with the tag, listener,
  speaker and ID asked for (any, if none is asked), not `DoNotPlay`, and
  their conditions met. Asked for by tag, which names one line, any
  table's line will do, whoever the player is: CM10_KiraL1A's cut scene
  goes on with `START_K10A2`, a line of Kira's marked as Worf's mission
  (W10), in another mission's table. A line only ever reached from another one (the
  middle of a conversation) is left out unless its ID is asked for. The
  best is one of the map's own mission, then one of a mission over a
  general one, then the one said fewest times, then any of those. It
  returns how many would do.
- **Conditions:** each test is the dialogue input's event of that name
  (`CTVE_DialogInput`: `LowHealth(30)`, `HasItem`, `SpecialCondition`...),
  its arguments converted to its parameters' types. `DS9Phase(n)`, which
  no script defines, is taken as the player's `OpsPhase` being n, as the
  scripts' own `OpsLevel` reads it. A test with no event is false and
  counted. `TestCondition` asks the same.
- **`IsDialogLineSpecial`:** a line of a mission, not a general one. The
  HUD shows that a crewmate has something to say when hailing them would
  find one (`CheckSpecialComm`).
- **`StartDialog`** (and `PlayDialog`, which calls it) plays the best
  line, then the ones it leads to. It returns a thread number (0 if no
  line would do), and `IsDialogThreadPlaying` stays true until the last
  line is done. Each line:
  - who says it, and to whom: the actor's `GetSpeakerActor` for their
    codes (one near the camera, else the HUD's picture of someone on the
    communicator);
  - the level's `Talker`, `Listener`, `CurrentLineType` and
    `CurrentLineIndex` set for the scripts (the heads look at the
    talker, the cut scenes' directors aim at it);
  - its event set off (`DoSpecialEvent`, then `DoEvent`: the actors with
    that tag triggered), its animations played (`PlayDialogAnim`) and the
    speaker's face set (`FacialExpression`, by name);
  - the HUD told (`HudText`), and the Maps page shows it above the status
    line;
  - said for as long as its voice lasts: the length of the WAV in its
    Sound (O'Brien's answer on M01_KiraL1A, 5 seconds), else about 15
    letters a second;
  - while it's said, the speaker's mouth moved (`C3DSpeechEvent` a tenth
    of a second apart, the shape from the letter being said). This is
    Reluva's own: the game's lip-sync data isn't read;
  - then its pause, then the line its NEXTLINE names (`?` any character;
    of those that fit, the best as above), or the end: the talker and
    listener cleared.
- **`DialogTick`**, the dialogue input's each tick while the HUD allows,
  does nothing. The lines play on the level's own tick, the cut scenes'
  too, which it isn't called in. What else the game's engine did there
  isn't known; most likely the AI's own lines (AMB, KIL, FSE...), which
  no script asks for.
- On M01_KiraL1A, the opening cut scene's line is shown as Kira says it.
  Hailing O'Brien plays Kira's line, then his answer through its
  NEXTLINE. Over all maps' first 20 seconds, 33 dialogues start and 3
  calls find no line, none of them in the tables: a training line
  (`TRAIN01`), CM11_LabRitual's `START_G11B`, and a speech trigger on
  M04_SiskoL1 with no type.

Not done: the voices aren't played (nothing makes a sound yet);
RETURNLINE (the communicator's menus) and the CAMERA column; the AI's
own lines.

### Documents and objectives

`vm_documents.cpp` reads The Fallen's other text files in `int`
([int.md](../formats/unreal/int.md)) for `CTVE_Document`, which the HUD
spawns. At the start of a level (its `Timer`), the document actor does
three things:
- **Lists the documents** the player has, by asking `FindDocument` for
  `Document_0` to `Document_29`. A section of `Document.int` with that
  name is there: `FoundDocument`. That reads its settings and lists the
  document if its `MissionLevel` is at most the player's mission
  (`GetMissionLevel`). Mission 1 has none, mission 5 ten, mission 11 all
  eleven.
- **Reads the objectives:** each `Objective_N` of the map's own `.int`
  (`GetMapTitle()` + `.int`). Its title and whether it's critical and
  active go into LevelInfo's `Objective_Title`, `Objective_bCritical`
  and `Objective_State` (0 active, −1 not yet). M01_KiraL1A starts with
  7, three of them active.
- **Reads the level's title** from `SaveGameString` into LevelInfo's
  `Title`: M01_KiraL1A is "Bajor - Monastery".

The natives:
- `GetMapTitle()`: the map's name (`M01_KiraL1A`). Its callers make a
  file name of it, or show it in capitals.
- `FindDocument(Section)`: sends `FoundDocument(Section)` if
  `Document.int` has that section.
- `LocalizeDocument(Name, File)`: the section's settings become the
  actor's properties of the same names (`Title`, `MissionLevel`,
  `bOnlyThisMission`, `bCritical`, `bActive`), each converted to its
  type. As localised properties are, by name.
- `LoadDocument(Name, File)`: the section's lines, in order, each to the
  actor's event that draws it: `Color` (`SetColor`), `Text`
  (`LoadText`, a line), `IMAGE` (`DefineImage`), `Title` (`SetTitle`).
  An image's `Justify=` becomes the HUD's justification: it takes 2 as
  right and 3 as centred, anything else as left. Those events draw
  through the HUD onto its canvas, which isn't drawn yet. They're called
  (the Grigari report: 12 lines and an image), and they stop at the
  HUD's missing canvas.

The Maps page shows the level's title and its active objectives while
playing.

### The player's input

`ScriptLevel::controls` (forward, strafe, turn, look up, each −1 to 1)
is what the keys would say. Each tick it sets the input axes the way the
engine does from DefUser.ini's bindings: `aBaseY` ±300 for
`MoveForward`, `aStrafe` ±300, `aBaseX` ±150 for `TurnLeft`/`TurnRight`,
`aLookUp`, and the rest 0. Then come `PlayerInput` and `PlayerTick`. The
Fallen's scripts work out the rest:
- `CTVE_PlayerPawn.PlayerInput` turns `aBaseY` into `aForward` and
  `aBaseX` into `aTurn`. It zeroes them all while the HUD's letterbox is
  up, in a cut scene.
- `PlayerWalking.PlayerMove` takes 0.4 of them for the acceleration (a
  walk: 96 units a second).
- `UpdateRotation` turns her.

`reluva-tool ue-run` reports what moved on each map. A test harness
walking a player forward, turning, forward again, then strafing, on
TrainingRoom, M01_KiraL2, M03_WorfL1, M04_SiskoL1 and M01_KiraL1A after
its opening: she walks, turns, stops at walls and in corners, slides
along them, steps down off slopes, and doesn't leave the level.

### Splines

The Fallen moves its cameras and its actors in cut scenes along splines.
These are chains of `CTVE_Spline` nodes; `CTVE_Director` is a camera's,
`CTVE_SplineAnim` an actor's. Their scripts ask the native
`GetSplineData` each tick where along the chain they are at a time
(`vm_splines.cpp`). UnrealEd saves all it needs with each node:
- `TimeLine`: when the chain reaches it.
- `Duration` and `Length` of the segment after it.
- `Tangent`: on The Fallen's, the head's is exactly half the way to the
  next node, Catmull-Rom's.
- `AveSpeed`: the mean of this node's and the next's `Speed`.

So Reluva's reading is:
- **Timing.** Each node waits out any time the segment's `Duration`
  leaves over, then travels it, its speed changing evenly from its own
  `Speed` to the next's.
- **The path.** The segment is a cubic through the two nodes, leaving
  and arriving along their tangents (a straight line with
  `Interpolation` linear). It's followed by length, measured in 16 pieces.
- **Facing**, by the node's `Focus`:
  - Direction (the default): turning from each node's own facing (the
    arrow UnrealEd draws on it) to the next's.
  - Spline and orient: along the curve.
  - Actor and actor follow: at the actor tagged `FocusActorTag` (found
    once, kept in `FocalActor`), offset by `FocusOffset` turned with it.
  - World: at `FocusOffset`.
  - Then `LocalRotation` is added, and `RollDegrees` turns it, changing
    evenly to the next node's.
- `TimeAlpha`, how far along the segment, is what directors take the
  field of view between their nodes by.

On M01_KiraL1A this plays the opening as written: cameras glide through
the monastery while Kira and the monks walk their paths. After about 80
seconds the letterbox goes and she can be moved.

## In the Maps page

Loading a map runs its start, the player's login and a second of play
(`runLevelStart`),
then compares each placed actor with how it was saved: destroyed,
hidden or shown, drawn another way, its light's type or brightness. The
lightmaps, sprites and meshes use the result: lights the scripts turned
off light nothing (trigger lights not initially on, which the original
starts dark), editor icons hide themselves (`CTV_TriggerLight` sets
`DrawType` to none), actors hidden until something happens stay
hidden. The info shows what changed; P turns the scripts off (the level
as saved). On M01_KiraL1A: 64 lights changed (37 triggered lights off),
10 Bajoran fanatics hidden until their ambush, 9 exploding walls' icons
gone.

## Animation

`vm_anim.cpp` plays actors' animations as the scripts ask, each tick
after physics. It's Reluva's reading of the public UnrealScript
documentation of these natives and of what The Fallen's scripts expect.
An actor's animation is its own properties:
- `AnimSequence`.
- `AnimFrame`: 0 to 1 through it; below 0 while tweening into it.
- `AnimRate`: how much of it a second.
- `bAnimLoop`, `bAnimFinished`, `AnimLast` (where its last frame
  starts), `TweenRate`.

The sequences are the actor's mesh's: its own, and those it names
whose frames are in the shared sets (`Mesh::otherAnims`), each with a
frame count and frames a second.

- `PlayAnim(Sequence, Rate, TweenTime)` plays it once: `AnimRate` is
  `Rate` times the sequence's frames a second over its frame count.
  With a tween, it first tweens into the first frame over `TweenTime`.
- `LoopAnim` does the same, looping. If it's looping that sequence
  already, only its rate changes (scripts call it every tick).
- `TweenAnim(Sequence, Time)` goes to its first frame over `Time` and
  stays.
- A sequence its mesh doesn't have (or an actor without a mesh) is
  finished at once, so nothing waits on it.
- Each tick moves `AnimFrame` on.
  - Passing a notification's time calls the function it names on the
    actor. These are the mesh's own, kept with each sequence (78 of
    The Fallen's meshes have them, 3,190 in all): footsteps, AI firing
    their weapons (`FireWeapon`), the jump's push (`ApplyJumpVelocity`),
    and more. No script sets `bAnimNotify`, so they're called whenever
    a sequence has them.
  - At the end, `AnimEnd` is sent. A loop goes on round (and sends
    `AnimEnd` each time). One played once stops at its last frame,
    finished.
- `FinishAnim` (latent: The Fallen's scripts call it 638 times) waits
  until then: the end of the animation, or of this round of a loop. It
  returns at once if nothing's playing. `IsAnimating` says whether one
  is playing or tweening. `GetAnimDuration` gives a sequence's length
in seconds at a rate. `HasAnim` and `GetAnimGroup` read the
  sequences, borrowed ones included (`runf` is Kira's, not only her
  mesh's own).

On M01_KiraL2:
- Kira's idle loops.
- Running plays `runf`, round every two-thirds of a second.
- Stopping plays an idle look-around.
- Jumping plays `Jump` (0.4 s), whose notification pushes her up about
  77 units. Its `AnimEnd` then starts the in-air loop, `LeapIdleU`.

### The Fallen's layered animation

The Fallen's meshes built on skeletons can play an animation on part of
the skeleton over the actor's own. Its scripts do it through `C3D`
natives:
- **Melee:** a punch or weapon swing while running (`CTV_Human`, on
  `UpperTorso`, from `Bip01_Spine`).
- **Using things:** the player's activate gesture.
- **Cut scenes:** the animators' upper or lower body.

`vm_anim.cpp` keeps each actor's layers, one per group.
- **Starting:** `C3DPlayAnim(Group, Sequence, Boned, Rate, TweenTime,
  InheritBone, ...)` and `C3DLoopAnim` play the sequence on the bone
  named and those under it. A group named lower (`LowerTorso`) is the
  rest of the skeleton. It fades in over the tween, its weight rising
  from 0 to 1.
- **Holding a pose:** `C3DTweenAnim` fades to the first frame.
- **Ticking:** a layer moves on at its rate, calls the sequence's
  notifications, and at its end (and each loop's) sends
  `C3DAnimEnd(Group)`. The scripts answer that with
  `C3DTweenOutAnim(Group, 0.1)`, which fades it out, and then it goes.
- **Waiting and asking:** `C3DFinishAnim(Group)` (latent) waits for that
  end, and `C3DIsAnimating(Group)` says whether one's playing.

Faces are channels:
- `C3DPlaySkinAnim()` hands out a tag. `Pawn` asks for one each for
  `Phonemes`, `Blink`, `EyeBrows` and `Mouth` if its mesh has them.
- `C3DSetSkinAnim(Tag, Sequence, FrameTime, Weight)` and
  `C3DTweenSkinAnim(Delay)` set a channel to a time in the sequence
  (seconds: the scripts pass frame × 0.033), tweening there.
- `C3DClearSkinAnim` and `C3DCopySkinAnimState` clear and copy them.
- The engine sends `UpdateHeadAnimation` to each pawn with one of
  The Fallen's meshes every tick. It sets the face's expression and
  blinks, and where the head looks. It returns at once for a pawn not
  drawn in the last four seconds (`LastRenderTime`, which the Maps
  page's drawing sets).
  - To pick whom to look at, it goes through the pawns within 800 units
    (`RadiusPawns`). It weights each by how near it is, more for the
    one talking and the enemy, times what `CheckFOV` says of it.
  - It then turns the head (`C3DApplyWorldRotation` on `Bip01_Head`)
    toward the chosen pawn's head, eased and kept within limits.
  - On DS9_Ops, up to 12 heads look at someone at once. Each head is on
    average 22° off the line to its target, where the body's facing is
    37° off.
- `CheckFOV(Pos, Rot, Target, Radius, CosFOV, out BestHitLocation,
  bCheckExtents, bCalcVisible, LocationZAdjust, HeightAdjust)` says how
  much of `Target` is seen from `Pos` looking along `Rot`. The AI's sight
  uses it, and so do the turrets, the player's auto-aim and the heads.
  How it works is Reluva's reading of how the scripts use it: they
  compare the result against 0, 0.1, 0.3, 0.5 and 0.99, and the heads
  multiply a weight by it.
  - It returns 0 when the target's middle is beyond `Radius` or outside
    the cone `CosFOV` is the cosine of. The middle is its Location raised
    by `LocationZAdjust`; the heads pass the head's height.
  - The points it looks for are the middle, and with either flag also
    the top, bottom and sides of the target's collision cylinder, whose
    height is grown by `HeightAdjust`. A point is seen if nothing in the
    world is in the way (as `FastTrace`).
  - With `bCalcVisible`, it returns the share of the points seen.
    Otherwise it returns 1 if any is seen, which is why the turrets'
    check for more than 0.99 can pass with `bCheckExtents`.
  - `BestHitLocation` is the first point seen, the middle first. The
    turrets aim there.
- `C3DUsesC3DMesh` says whether the mesh is in The Fallen's own layout
  (its imported-model block), and `C3DIsBoned` whether it's built on
  bones. `Pawn` sets its face's channels up only if the first is true.

Drawing a layer (`LayeredPoser`, `mesh.cpp`): the frames posed when a
mesh is read are one animation each, so an actor with a layer showing
is posed at draw time instead.
- Each bone takes its place in its parent from the actor's own
  animation. Those of the layer's part are blended towards the layer's
  animation's by its weight.
- Keys come from the mesh's own clips, or the shared set's for an
  animation past its own frames, as borrowing does.
- The vertices are placed as reading does, and that frame is uploaded
  for the frame drawn.
- With no layer, it gives exactly the frames read: no difference on
  Kira's idle, run, or the punch borrowed from the shared sets.

On M01_KiraL2, firing while running throws `MelRunPnchL`. The frame
drawn mid-punch has her legs exactly as in the run without it, and her
upper body turned into the punch.

Not done:
- **Faces:** drawing them. Their sequences are vertex animations
  ([mesh.md](../formats/unreal/mesh.md): `Blink`, `EyeBrows` and `Phonemes` are the clips with
  vertex keys), still to be combined with the bones' pose.
- **Layer options:** the layers' `ClearYaw`/`ClearPitch`/`ClearRoll`.
- **Speed-scaled rates:** a negative rate, which no script uses.
- **Tweens:** blending from the old pose (drawn at the first frame).

### Bones and attachment points

`vm_attach.cpp` says where parts of an actor's mesh are: its bones, its
attachment points and its precomputed places ([mesh.md](../formats/unreal/mesh.md)).
Each is worked out in the mesh's animation now:
- **The pose:** posed through its skeleton with its layers (once a tick,
  kept for the tick), or between its frames. A bone moves with whatever
  layer plays on its part: mid-punch, Kira's left hand is 30 units
  further forward than standing, while her feet don't move.
- **Placing it:** placed as the Maps page draws the mesh, so they agree
  (`placeMeshVertex`, then the actor's Location, PrePivot, Rotation and
  DrawScale).

The natives:
- `C3DGetJointWorldPos(Bone, out Location, out Rotation)`: a bone's place
  and axes. The cut-scene cameras aim at a speaker's `Bip01_Head`.
  `C3DGetJointLocalPos` gives it from the actor's own location, unturned.
  - **The axes are in the scripts' order:** X is the skeleton's -Z, Y its
    -X, and Z its Y. This was worked out from what the scripts do with
    them, and fits Kira's skeleton in every case:
    - a head's -Z is where it looks (`Pawn`'s look-at, `CTVE_Pawn`'s
      sight);
    - the eyes, nose and mouth are along -Z and up -X (`NosePos`,
      `CTV_ColdBreath`);
    - a foot standing flat has Z along the ground's normal and X
      toward its toes (`CalcFootZ`).
  The `Rel` forms are taken as relative to the bone's parent:
  - `C3DGetJointWorldPosRel` is the step from the parent bone to this
    one, in the world's axes. Its rotation is the bone's turn from the
    parent's.
  - `C3DGetJointLocalPosRel` is that step in the parent's own axes: the
    skeleton's offset for the bone, as posed, times DrawScale. Both the
    step and the turn use the scripts' order of axes.
  - No script calls them, so what they're meant to give is a guess
    from their names.
- `C3DDoesBoneExist`.
- `C3DGetAttachPtCoords(Name, out Pos, out Orientation)`: an attachment
  point in the world (`GunTip`, `FirePoint`, `KickPoint`...). On a
  triangle it's the triangle's frame (above) with the point's offset
  and turn; on the origin, its offset and turn from the mesh's float
  copy.
- `C3DGetAttachPtCoordsPrecomputed`: the places worked out in advance
  (`FireGunTipLoc`).
- `C3DGetAttachPtOffset` (from the actor), `C3DGetNumAttachPts`.
- A part the mesh hasn't answers with the actor's own location and
  rotation, so what starts there starts at the actor.
- **Attaching:** `C3DAttach(Obj, ObjPt, ThisPt)` keeps this actor, from
  each tick on, with its point `ThisPt` on `Obj`'s `ObjPt`. It's turned
  so its point's axes are `Obj`'s point's, and moved so the two meet.
  This runs after every actor's tick, until `C3DDetach`.
  - What's attached in the levels: the ensigns' tricorders and phasers
    on their belts (`BeltAttach` on `BeltClip`), and the gun nests'
    spotlights.
  - What's attached in play: the flashlight on the wrist, the breather
    on the face, the bat'leth in the hand.
  - The player's guns aren't attached. `UpdateEyeHeight`, which the
    engine sends the player each tick after it moves, calls the game's
    `UpdatePlayerIK`. Its `ApplyWeaponIK` puts the gun in front of her
    chest, aimed where she looks, and sets IK targets for her hands on
    it (below).
- **Before drawing:** the Maps page sends `C3DPreRender` to each mesh it
  is about to draw. The player's weapon then works out its tip
  (`FirePoint`, as `WeaponTipLocation`) and traces the crosshair.
- `C3DActorInit` and `C3DInitAltMeshes` set up The Fallen's renderer, so
  they do nothing here.

**Bending the bones.** The scripts change a pose on top of its
animations. What they set is kept per actor until they change it. The
pose for drawing, for the bone natives and for the attachment points
includes it, in this order:
- **`C3DApplyRotation(Bone, Axis, Angle)`:** degrees about one of the
  bone's axes (in the scripts' order), turning as a rotator's `Pitch`,
  `Yaw` or `Roll` would. In the skeleton's own axes, Pitch is about X,
  Yaw about Y and Roll about Z. A positive Pitch bends the back and nods
  the head forward, which is what the scripts mean when the player looks
  down (`ApplyBackBend`). The upper arms' Roll then swings them back
  against the bend.
- **`C3DApplyWorldRotation(Bone, Rotation)`:** the bone turned where it
  is, its children with it. The turn is taken in the actor's axes, not
  the world's: the scripts build these from the player's own yaw and
  pitch (the hips' and spine's twist in `ApplyBackTwist`, a head turning
  to look at someone in `Pawn`). In the world's axes, a pitch would tip
  the head sideways whenever the actor faced along Y.
- **The IK chains:** `C3DSetIkTargetPositionAbs` / `C3DSetIkTargetOrientationAbs`
  (in the world), and `C3DSetIkTargetPosition` / `C3DSetIkTargetOrientation`
  (from another actor's place and turn). Each takes a chain's name and a
  weight that blends it in. `C3DClearIkTarget` lets go.
  - The chains aren't in the meshes, so the engine must know them by
    name. `LeftArm`/`RightArm` move a hand to a place and
    `LeftHand`/`RightHand` turn it; `LeftLeg`/`RightLeg` and
    `LeftFoot`/`RightFoot` do the same for the feet. Each bends the end
    bone's two parents: upper arm, forearm, hand (`Bip01_R_Hand`), or
    thigh, calf, foot.
  - **What reaches.** For a hand it's the mesh's attachment point of the
    same name: the player's `RightHand` goes onto the gun's `RightHand`,
    turned to its axes, as `C3DAttach` would put them. For a foot it's
    the bone itself, in the scripts' axes, since the scripts work the
    targets out from `C3DGetJointWorldPos` (`CalcFootZ`).
  - **The solve.** The upper bone turns so the elbow or knee is where the
    two bones' lengths put it on the way to the target, bending in the
    plane it bends in now. The middle bone points at the target. When
    the hand is also turned, the forearm rolls to match it, so the wrist
    keeps the bend the animation gives it.
  - **Correcting for the skin.** A hand's grip point is on skin at the
    wrist that the forearm moves too. So each tick the point is measured
    on the hand once without the IK and once with it, and the solve is
    done again from that. On M04_KiraL1, with the phaser rifle drawn,
    Kira's grip points are 0.5 and 0.0 units from the rifle's, and their
    turns agree to within 10°.
- **`InterpolateRotation(A, B, Alpha)`:** Alpha of the way from A to B,
  each of pitch, yaw and roll the shorter way round. The scripts ease
  the IK turns and the head's look with it. While it was missing, every
  foot target came out as turn 0, which faced the feet backward.

On M01_KiraL2, standing, Kira's points and bones are:
- head bone 35 above her middle (her top is 47);
- feet 40 below;
- hands at her hips, 14 to 15 units to each side, the right on her
  right;
- `Face` in front of her head.

On M11_KiraL1, an ensign's tricorder hangs upright at her belt. On
M04_KiraL1, once 2 is pressed, Kira raises the phaser rifle to her right
shoulder with both hands on it. Its tip is 17 units ahead.

Not done:
- How a triangle's frame is set is a guess, so a point's turn may be
  off, though not its place, since the offsets are a few units at most.
- The meanings above are Reluva's readings of how the scripts use the
  natives: the order of a bone's axes, the axes `C3DApplyRotation` turns
  about, the actor's axes for `C3DApplyWorldRotation`, and what reaches
  in an IK chain.
- `C3DApplyIKOnBasePose` and `C3DRemoveRotation` aren't written; no
  script calls them.

## Playing a level

`LevelPlay` (`level_play.cpp`) is a level being played. It starts the
level as above and logs the player in. Then, as real time passes, it
ticks the level in steps of a thirtieth of a second, at most four a
frame: when the scripts can't keep up, the game slows down rather than
falling further behind. The Maps page plays one with Enter:

- **Controls** are the player's input axes and buttons, set each tick
  as the engine sets them from the keys (above). W/S move, A/D step
  aside, Left/Right turn, Up/Down look, and dragging the mouse turns
  and looks (`aMouseX`/`aMouseY`, used up by one tick). Space jumps
  (`bExtra1` and `aUp` held, and the `Jump` exec function called once).
  Ctrl or F fires (`bFire` and `Fire`). The number keys call the exec
  functions the game binds them to (`[Engine.Input]` in User.ini, else
  DefUser.ini: 2 is `Rifle`, 0 `HolsterWeapon`).
- **The camera** is where the game puts it: the page calls the player's
  `PlayerCalcView` each frame, as the engine does before drawing. The
  Fallen's is third person, and all script: `CTVE_Camera.CameraAI`
  follows the player, swings aside as they run and pulls in out of
  walls, and follows the cut scenes' directors. With it come the field
  of view (`FovAngle`) and the HUD's letterbox. When the camera is
  ahead of the player, `CameraAI` writes its direction with the yaw
  turned round and the pitch past straight up (`Pitch = 98304 −
  Pitch`). So the page takes the view's facing from the rotation's
  direction, not its yaw and pitch alone, which would look straight
  down.
- **What's drawn** is the level's actors as their properties are now,
  read each frame (`LevelPlay::look`, through each class's property
  slots, found once):
  - Meshes where they stand, turned, scaled, in their render style
    (modulated: the player's floor shadow, a `CTV_Shadow` quad), posed
    where their animation is (`AnimFrame`, below). A character's
    animation from the shared sets is borrowed the first time it plays
    one its mesh lacks (the run, `runf`). While tweening into an
    animation, it's drawn at the animation's first frame.
  - Sprites.
  - Movers where their scripts have moved them.

  Lighting is the level's lightmaps, as worked out when it loaded.

While playing, V lets the camera loose: the keys fly it round the level
as it plays on, the player standing, and V again gives it back to the
game.

`reluva-tool ue-play` plays the page without a window, through a
software renderer (`tools/soft_renderer.cpp`: textured, depth tested,
blended triangles, enough to see a frame), driven by steps:
`play`, `wait <s>`, `hold <key> <s>`, `down`/`up <key>`, `press <key>`,
`watch <actor>` (the camera let loose in front of a live actor),
`drag <dx> <dy>`, `shot <file.ppm>`. Checked that way:
- **M01_KiraL2:** Kira runs down the stone corridor from behind, the
  camera swinging to her side, and turns into the wall with the camera
  pulled in beside her.
- **M04_KiraL1:** she rides the lift down from her start and stands in
  the turbolift.
- **M01_KiraL1A:** the opening's directors fly over the monastery
  bridge, then follow her and a monk, letterboxed.

**Speed**, measured without drawing (`ue-play`, M01_KiraL2, 20 s, 600
frames): 13 s in all, about 22 ms a frame.
- The scripts' ticks take 3.4 s.
- The page's own work (mostly the fire and water textures) takes 5.3 s,
  as it does when not playing.
- The rest is reading the live actors and the camera each frame, plus
  starting play and loading meshes once.

The biggest maps' ticks take twice as long. There, and with drawing
added, a slower machine falls below 30 frames a second, and the game
then runs slower than real time.
