# The UnrealScript virtual machine

Status: **runs The Fallen's levels with the player in them**: the
Maps page plays a level (Enter), its scripts live, the player moved by
the keyboard and mouse, seen from the game's own camera. It isn't the
game yet: AI has no paths, nothing makes a sound, and the HUD and menus aren't drawn. Every class of the game loads (`vm.cpp`), every default
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
the visible ones by radius, `AllPawns`), the pawn list, animation
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
- 62 are walking.
- 7 are still frozen in their map's opening cut scene.
- 4 started in water and swim.
- On CM_Endcredits, the credits, the player falls: there's nothing under
  its start.

The movement: 187,000 moves, 6,837 landings, 8,341 touches.
- 9,908 placings refused: scripts putting things where there's no room,
  mostly SR3_Raptor and DS9_SmallFishSchool on M05_SiskoL1b, again and
  again at one spot.
- 367 walks to a goal given up: SR3_Leech mostly.

What's left before play, by what `ue-run` reports:

- **Natives not written** (45): The Fallen's own skeletal meshes
  (`C3DActorInit`, `C3DIsBoned`, `C3DGetAttachPtCoords`...), its
  `ListActors` iterator (an AI's list of known actors, not yet
  understood), `CheckFOV`, `GetRendererFlags`. The AI's paths
  (`FindPathToward`, `RadiusPathNodes` and the other path node
  iterators). Its dialogue (`FindBestDialogLine`, `StartDialog`,
  `DialogTick`) and documents.
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
  once; reported).

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
`pointReachable` / `actorReachable` (a box swept there).

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
  is playing or tweening. `HasAnim` and `GetAnimGroup` read the
  sequences, borrowed ones included (`runf` is Kira's, not only her
  mesh's own).

On M01_KiraL2:
- Kira's idle loops.
- Running plays `runf`, round every two-thirds of a second.
- Stopping plays an idle look-around.
- Jumping plays `Jump` (0.4 s), whose notification pushes her up about
  77 units. Its `AnimEnd` then starts the in-air loop, `LeapIdleU`.

Not done: The Fallen's own layered skeletal animation (`C3DPlayAnim`,
`C3DTweenSkinAnim`... 26 calls in its scripts) and `C3DFinishAnim`
(returns at once); rates scaled by speed (a negative rate, which no
script uses); and blending from the old pose while tweening.

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
  Ctrl or F fires (`bFire` and `Fire`).
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

`reluva-tool ue-play` plays the page without a window, through a
software renderer (`tools/soft_renderer.cpp`: textured, depth tested,
blended triangles, enough to see a frame), driven by steps:
`play`, `wait <s>`, `hold <key> <s>`, `down`/`up <key>`, `press <key>`,
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
