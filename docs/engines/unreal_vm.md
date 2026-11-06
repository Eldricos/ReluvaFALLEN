# The UnrealScript virtual machine

Status: **runs The Fallen's levels with the player in them**: the
Maps page plays a level (Enter), its scripts live, the player moved by
the keyboard and mouse, seen from the game's own camera, the AI finding
its way along the level's paths and the characters' lines shown as
they're said, and its own HUD and menus drawn over it, the menus
travelling from map to map, games saved and loaded, and its sounds and
music heard. It isn't the
game yet: some of The Fallen's own engine functions are still missing
(below). Every class of the game loads (`vm.cpp`), every default
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

Objects the VM doesn't run are given their own stored properties over
their class's defaults when they're first met: textures always, anything
else under 64 KB (meshes and sounds aren't read whole for it). The
Fallen's HUD textures keep where they go on the screen in theirs (below).

Classes' `config` properties take the player's and the game's settings
from the `.ini` files as the class loads (`Vm::applyConfig`):
- a `config` property from the class's own section (`[Engine.HUD]`);
- a `globalconfig` one from the section of the class that declares it;
- User.ini first, then DefUser.ini, then the game's own `.ini`;
- an array's elements as `Key[n]`; structs aren't read.

So the HUD shows dialogue (`bDisplayDialog=True`) but not cut scenes'
subtitles (`bDisplaySubtitles=False`), as DefUser.ini has it, and the
player's mouse sensitivity is 3. A console command's `set` (the options
windows, below) changes a setting for as long as the VM runs, before the
files' (`Vm::setConfig`); the files aren't written.

Classes' `localized` properties, the text players read (menu items,
messages), come from the game's `.int` files (`Vm::applyLocalized`): the
class's own section, by its name alone (`[DS9_Hud]`), of its package's
`.int`, else of `Classes.int`, where The Fallen keeps them all (2,131
lines). Each is looked for in the `int` folder beside System, then in
System, where the engine's own are (Engine.int, UMenu.int, SoftDrv.int).
Without them the HUD's menus had bullets and no words ("RESUME GAME",
"MISSION OBJECTIVES"...). `Localize(Section, Key, Package)` reads the
same files, without the quotes the engine's put round their values. One
that's missing is `<?int?Package.Section.Key?>`, as the engine marks it.

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
- Control: jumps, `switch`/`case` (below), `foreach` (the iterator native gives
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

Classes with no script (Mesh, LodMesh, Model, Primitive, Sound, Font...)
are made up the same way when an object of theirs is first met
(`Vm::nativeClass`), as kinds of Object, except LodMesh is a Mesh and
Mesh and Model are Primitives, so `Mesh(...)` casts work. A name looked
up by `DynamicLoadObject` is taken only as an object of the class asked:
`PLYR.DS9_Sisko` is both a class and a mesh.

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

A `switch` finds its case itself: the labels in turn, each one's value
against the switch's, jumping on to the next label until one matches or
the default. A `case` label reached by falling through from the case
above is passed over, not tested again. Labels with no code between them
share the code after the last (`case 38: case 40: case 13:` in the
console's `KeyEvent`). Testing each label as it was reached took the
first match on to the next label, failed it there and jumped past the
shared code: the menu's arrows never reached the HUD.

## A level

`ScriptLevel` makes a map's actors from the level's actor list (the
Level object's: a count, then compact references; the first is the
LevelInfo), each with its class's defaults then its saved properties,
their `Level`, `XLevel` and `Outer` set. Then `beginPlay`, in
`UGameEngine::LoadMap`'s order as read from Engine.dll (the code is
Reluva's own):

1. **No events yet.** The engine passes an actor's events to its
   script only once `LevelInfo.bBegunPlay` is set (`AActor::ProcessEvent`),
   so what happens before that is heard by no script (`Vm::holdEvents`).
2. `bHighDetailMode` set. The game type (the LevelInfo's
   `DefaultGameType`, else the .ini's `[Engine.Engine] DefaultGame`:
   `DS9.DS9_GameInfo`) spawned: in silence, so it never gets `Spawned`.
   `TimeSeconds` and `NoPauseTimeSeconds` 0.
3. **The ghosts set aside** before anything starts: each actor placed
   with The Fallen's `bIsGhost` (a guard who arrives later, an effect for
   a scene) goes to the ghost list. Every actor's `Touching` cleared.
4. `bBegunPlay` and `bStartup` set, then the game type's `InitGame`.
5. **Six passes** over every actor, the GameInfo and LevelInfo too,
   each reaching actors spawned during it: `SetInitialState` (its
   initial or auto state); `PreBeginPlay`; `BeginPlay`; its zone (the
   one it's in: `ZoneChange` if that's a ZoneInfo's, nothing to the
   zones); `PostBeginPlay`; its base (with an `AttachTag`, the last actor
   so tagged; else a decoration, item or pawn colliding with the world,
   standing still and with none, takes what's 8 units under it).
6. `bStartup` cleared.

Spawned actors get `SetInitialState`, `Spawned`, `PreBeginPlay`,
`BeginPlay`, their zone, then `PostBeginPlay` (and `bHasBegunPlay`).
Destroyed ones get `Destroyed` (and their owner `LostChild`); `bStatic`
and `bNoDelete` actors refuse to go.

**Ghosts** (`MakeGhost`, `GhostActors`): made a ghost, an actor leaves
the level as if destroyed, without being destroyed: `EndState`, no base
and nothing based on it, what it owns without an owner, `UnTouch` both
ways, its owner's `LostChild`. Brought back, one placed as a ghost
starts then, as if spawned; one that had begun play only gets its zone
and base again. `GhostActors` with no tag, asked to bring them back,
does nothing (the engine refuses to revive a whole class at once).

**A tick** (`ULevel::Tick`, `AActor::Tick`): the time scaled by
`TimeDilation` and counted (`TimeSeconds` unless paused,
`NoPauseTimeSeconds` always), then held within 0.005 to 0.4 seconds as
the actors' `DeltaTime`. Each actor that isn't static, in the level's
order (those spawned during the tick too): its animation (`AnimEnd`,
notifications); the player its input, `PlayerInput` and `PlayerTick`
(no `Tick`), any other its `Tick`; its state code; its timer (at most
once a tick); its `LifeSpan` (at 0, `Expired` and gone, with no
physics); its physics; a player's `UpdateEyeHeight`. Paused, the player
only reads its input (`PlayerInput`, its axes 0) and only `bAlwaysTick`
actors tick; the dialogue goes on. The Fallen's generators (sparks, the
Jerrado bug swarm: one every 5 ms, each to live 0.4 s) count on the life
span: without it, M03_KiraL2's swarm had made 17,500 actors by 20
seconds in. Not done: the owner ticked before what it owns, the pain
timer, and The Fallen's sleep for actors in zones unseen for 5 seconds.

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

A point in no ZoneInfo's zone (zone 0, or one with none) is in the
LevelInfo's: it's the default zone, as in UE1. Left as None, the
player's `ViewFlash`, `UnderWaterSounds` and `HandleWalking` read None
150,000 times in all maps' first 20 seconds.

A pawn's feet, chest (The Fallen's `ChestHeight` up its height, 0.65)
and eyes are in zones too: `FootRegion`, `ChestRegion` and
`HeadRegion`, kept as it moves, and `FootZoneChange`, `ChestZoneChange`
and `HeadZoneChange` when one changes (wading, swimming, breath).

### Noise

The AI hears (`ScriptLevel::makeNoise`). A noise is `MakeNoise(Loudness)`
or a sound played without `bNoHearNoise` (as loud as its volume). Only a
pawn's noise is heard: its own, its weapon's or shot's, or what it set
off (its `Instigator`), as UE1's `MakeNoise` asks; the level's ambient
emitters aren't (they kept knocking guards out of their states). Each
other pawn hears it if Loudness x 800 over the distance (halved with the
world between) reaches its `HearingThreshold` (1 by default: a noise of
loudness 1 carries 800 units in the open). Then its `HearNoise(Loudness,
the maker)`: The Fallen's AI searches toward it, or attacks if it can
see the maker. One noise a pawn every fifth of a second. A guard with no
enemy, hearing the player, says its FHE line ("Did you hear that?").
UE1's own rule isn't in the scripts; this is Reluva's.

### The player logging in

After `beginPlay`, `login` does the engine's side of a player joining,
as `ULevel::SpawnPlayActor` does:

1. The character: The Fallen's starts (`CTVE_PlayerStart`) name it in
   their `Player` property (M01_KiraL1A's: `PLYR.DS9_Kira`). Of the
   starts that name one, enabled or tagged as the portal arrived by:
   with no portal the first, with one the one so tagged, else the last.
   Else the URL's class, else `PLYR.DS9_Sisko`. (The engine doesn't ask
   `FindPlayerStart` for this.)
2. The game type's `Login` with options `?Name=Player?Class=...`: it
   finds the start itself and spawns the pawn there (whose
   `PreBeginPlay` sets The Fallen's `Level.Player`).
3. The engine's own objects for it: a viewport (`Viewport` has no script
   class, it's C++ only: made up as a kind of `Player`, `Vm::defineIntrinsic`,
   so `Viewport(Player)` casts work) holding the .ini's console
   (`UMenu.DS9_Console`).
4. `Possess`; `bAdmin` (playing alone); `TravelPreAccept`; the game
   type's `AcceptInventory`; `TravelPostAccept`; the game type's
   `PostLogin`. Travelling inventory isn't done (a level starts afresh).

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

`ue-run`'s "drawn as nothing" and "lights at brightness 0" are the
game's own: over all maps, the actors with no DrawType are trigger
lights (489), exploding walls and breaking glass waiting to break,
decals (drawn as decals), the HUD's portrait actors, triggers and sound
actors; the dark lights are TriggerLights that start off (1,300) and
plain Lights placed with brightness 0 (226: TrainingRoom's, the
Promenade's and Ops' 30 each).

**Placed ghosts.** An actor saved in the map with The Fallen's
`bIsGhost` is set aside once the level has started it (its
`PreBeginPlay` to `SetInitialState`: a friendly AI takes its phaser
there), out of the level's list until an event brings it back through
`GhostActors`, which every `Trigger` and player start sends its event
through as well as to the live actors. Over all maps that's 1,306
actors: mostly the enemies of later waves (179 Jem'Hadar, 126 Bajoran
fanatics, 90 Grigari drones...), creatures, and a scene's lights,
sounds, music and speech. Left in, they played from the start: on
M01_WorfL1 an ensign who arrives in a later Grigari attack was already
fighting, with no phaser (925 of the run's None accessed). Set aside,
an all-maps run makes 178 million calls (229 before), and None is
accessed 3,860 times (12,600 before).

**What None accessed is left** (`ue-run` lists it by place in the
code, the context expression's offset as `ue-script` lists it). It all
reads as the game's own:
- `CTV_AiPawn.HearNoise` and `DS9_FriendlyAI.HearNoise` (about 900 of
  each place): `Instigator.Owner.IsA('HUD')` with no `None` test, when
  the noise's maker has no owner, as a pawn placed or spawned in the
  level hasn't (here as in UE1).
- `CTVE_PlayerActivate.AutomaticActivate` (841): `Level.Player`, which
  the player's own script sets once it has joined, read when a mover's
  `BeginPlay` touches the trigger, before any player has.
- `CTVE_PlayerPawn.PlayerInput` (600): `CTVE_Hud(myHUD).IsLetterBoxOn()`
  before the player's HUD is made, in its first ticks. The HUD is made
  by `InitPlayerStart` or the first `PreRender`; `ue-run` draws no
  frames, so it's a few ticks later there than in play.

What's left before play, by what `ue-run` reports:

- **Natives not written**: none that a map calls in its first seconds.
  The Fallen's own physics modes are Reluva's reading of the scripts
  (see Movement and Effects).
- **Speed**: the biggest map, M03_KiraL2 (4,263 actors), runs 20 seconds
  of play in about 5.7 s without drawing (21.6 s before the work below,
  10.7 before the last of it); all 74 maps take 371 s together, 800
  before the last of it, the slowest (M10_WorfL1A) 11.5. These are
  October's figures; the later work and November's figures are under
  Speed below. The last of it:
  - Only actors that may be seen are looked at to draw: not hidden, in a
    zone seen through the portals (`LevelPlay::mayBeSeen`). The rest
    aren't drawn, so they don't get `C3DPreRender` or `LastRenderTime`
    either, as in UE1 (whose scripts read `LastRenderTime` to know
    they're seen). Before, every actor got both every frame.
  - A script function that only returns, with nothing to give back
    (Actor's `C3DPreRender` and other empty events), isn't run
    (`VmFunction::doesNothing`).
  - `Value`'s readings of numbers and objects are inline; what an actor
    looks like and its ambient sound are read through each class's
    slots, not by name.
  - A character posed each frame is uploaded with its triangles' layout
    worked out once (`MeshTopology`), only its vertices new.
  - Lightmaps that changing lights reach are redrawn only while their
    zone is seen (once seen again if they changed meanwhile), and a light
    effect's pattern takes what it needs of the time once a frame
    (`PatternNow`) and of each texel once at load (`PatternPoint`).
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
and each other (`vm_physics.cpp`), as Engine.dll's `ULevel::MoveActor`
and the actors' `phys*` functions do it (read for what they do, written
here anew). The Fallen's ledge and ladder tests (`TestForLedge`,
`CheckClimbing` and their helpers) are read from it the same way (below).

- **A move** (`MoveActor`) sweeps the actor's box 2 units further than
  asked, through the world if it has `bCollideWorld`, then against the
  cylinders of actors that block it (`bBlockActors`, `bBlockPlayers` for
  players, not its owner or what it owns) and movers. Hitting something,
  it stops 2 units back along its way (not off the wall), and doesn't
  move if that's no further than 2. A mover is collided with through its
  brush's own BSP (UnrealEd builds one for each mover), with the move
  turned into the brush's coordinates where the mover is now. A mover
  without one is a box. Traces count movers as the world, so a pawn
  stands on a lift.
- **After a move**, in the engine's order:
  - if an actor stopped it, `Bump2` (hit location and normal) to that
    actor and then to the mover, then `Bump` likewise;
  - what it now touches (`Touch` and `UnTouch` both ways, the `Touching`
    list);
  - last, zone changes (`ActorLeaving` to the old zone, `ZoneChange`,
    `ActorEntered` to the new).
- **HitWall** goes through the engine's `processHitWall`:
  - none for a pawn running into a pawn;
  - for a pawn, only while it pushes (some acceleration) and the wall
    faces back along its way to its `Destination` (dot under
    `MinHitWall`; level, when walking);
  - a pawn whose script has no `HitWall` of its own just gives up its
    move (`MoveTimer` −1, `bFromWall`), unless falling.
- **Smooth moves** (`MoveSmooth`): the move, then along what stopped it,
  then, told `HitWall` only now, along both walls (`TwoWallAdjust`: the
  crease between them if they face apart). Three moves at most, never a
  step.
- **Placing** (`SetLocation`, and spawning an actor that has
  `bCollideWorld` or `bCollideWhenPlacing`) finds a spot as Engine.dll's
  `ULevel::FindSpot` and `AdjustSpot` do. A spot whose point is in the
  world's solid is refused. If the actor's box fits, it stays. Otherwise
  six tries in turn (down, then along X and Y, then up and the other
  ways), and then towards the eight corners. Each try draws a line from
  the spot a little past the box's edge that way (its extent + 2; to the
  corner itself for the corners). If the world or a mover is in the way,
  the spot is pushed back off it along the face's normal by (1.05 −
  how far along it was) × that extent (the corner's distance + 2), and
  kept if the box fits there. The pushes add up, so a later try starts
  where the last one left it. Then, if all that moved it less than
  √1.5 × the extent's length and it fits, it stays there. Else it is
  raised from the spot 20, 40 and 60 units.
  As SpawnActor does, an actor that fits nowhere isn't spawned (`Spawn`
  returns None, and the log says so). Over all maps' first 3 seconds,
  that's 4 times: a Cardassian hypospray 60 units wide where nothing that
  wide fits, and the like. Before, it was left at the world's origin.
  Starts are placed for the
  default pawn height (40), and The Fallen's characters are taller (Worf
  is 52), so a player starting on a lift begins a little into its floor
  and is lifted onto it.

Physics each tick, after the actor's scripts:

| Physics | What it does |
|---|---|
| Walking, the player | `calcVelocity` (below), then one move along the floor's slope (`FloorNormal`) with up to three slides. Each wall is told `HitWall` as The Fallen's player's walking state reads it: X how head-on (the move's direction against the normal), Y its flat speed, Z how fast it slides on. Head-on (under −0.8), it stops. A ledge or ladder there is tried first (below); then a step up (in Engine.dll, `TestForLedge`'s small-step branch, here the walk's own up, across and down). Then down onto the floor for real, `MaxStepHeight` + 2, sliding down a floor steeper than 0.65. No floor: back up, `Falling`, then falling. Its velocity is then what it really moved. |
| Walking, other pawns | `calcVelocity`, then in steps of at most 0.05 s when going far, up to 4 (8 for pawns not The Fallen's). What stops a move is stepped up (`stepUp`: up `MaxStepHeight`, across, down; a square wall tried as a step again, else slid along; landing on a slope steeper than 0.7 undone). It's kept 1.9 to 2.4 above the floor (0.7 or flatter). With no floor, a pawn that can't jump (`bCanJump`) or is walking (`bIsWalking`) goes back, stops, and gives its move up (`MoveTimer` −1). Others fall with the rest of the tick. Velocity is what it moved, flat. A CTVE pawn's rise or drop on a step is eased out of how it's drawn (`PivotOffset`, into `PrePivot`, at 10 a second). |
| Falling, the player | Gravity 1.4 times the zone's, times 1 − 2.2·dt (kept to 0.5-1), `AirControl` of `AccelRate` across, up to the terminal velocity. One move with up to four slides, `HitWall` (head-on-ness, speed) at the first only. Landing on anything flatter than 0.65. Its velocity is then what it moved. |
| Falling, everything else | Steps of at most 0.1 s (8 at most), each moving at the average of its velocity at the start and end, split at the top of an arc. A pawn's air control is less with something just ahead. Landing on more than 0.7 (`Landed`, then walking, or still for an actor, and the rest of the tick walked). Steeper: `HitWall` and along it, then along both. `bBounce`: `HitWall` and its script bounces it. |
| Swimming, flying | A pawn's only: `calcVelocity` in the fluid (`WaterSpeed` or `AirSpeed`, the zone's fluid friction; swimmers float by `Buoyancy` against `Mass`). One move; a flier steps up a low wall, else slides. Swimmers fall once out of water (Reluva's: the engine's `Swim` wasn't decoded). |
| Projectile | Its velocity plus acceleration; `HitWall` on the world. |
| Rotating | `RotationRate` each second, or toward `DesiredRotation` with `bRotateToDesired`. Only in this physics (and The Fallen's single-axis ones) does an actor turn; a pawn turns toward its `DesiredRotation` in any physics but `Spider` (not a player's pawn: its input turns it). A pawn's `MoveTimer` counts down. |
| Moving brush | While its script has it going (`InterpolateTo` sets `bInterpolating`): a mover from its old key toward its next by `PhysAlpha`, eased in and out if it glides by time. It carries the actors based on it and sends `InterpolateEnd` at the end. |

**`calcVelocity`**, the velocity towards the acceleration:
- **Acceleration:** only capped at `AccelRate`, at 30% of it walking or
  crouching.
- **Steering:** the velocity is turned towards the acceleration by the
  friction times dt.
- **Braking,** with no acceleration:
  - the player in one go, stopped under a speed of about 2.2;
  - the AI in steps of 0.03 s, stopped under 10.
- **Top speed:** `GroundSpeed`, `WaterSpeed` or `AirSpeed`, eased down to
  30% when walking or crouching.
  - The player's is ×0.5 strafing while walking or crouching (else ×0.8),
    ×0.8 backing, ×0.75 crawling.
  - Others' are times `DesiredSpeed`, which the player's isn't. Before,
    Reluva held the player to `DesiredSpeed` too, a third of its run.

The Fallen added its own modes (past 10): the player's climbing,
free flight, fish, interpolation and one for each kind of effect.
`vm_climbing.cpp` and `vm_effect_physics.cpp` work them. The player's
three (Climbing, Hanging, Clambering) are as Engine.dll's
`ACTVE_PlayerPawn::physClimbing`, `physHanging` and `physClambering`;
the rest are Reluva's reading of what the scripts set and leave undone.

| Physics | What it does |
|---|---|
| Interpolating, CTVE_Interpolating | Along a path of interpolation points (`SpecialEvent`'s player paths, the intro's camera), as physPathing and CTVE_physPathing: from `Target` to its `Next` by `PhysAlpha`, at `PhysRate` times the two points' `RateModifier` blended by the way along (backward with a negative rate). With a point before and after, the place is a spline through the four (weights x^4/16 - x^2/2 + 1 at t+1, t, t-1, t-2, summing to 1), as is the turn (Interpolating: the rotators' numbers as they are; CTVE: slerped between the two); else straight. Past a point: `InterpolateEnd` on the point, then the actor, then on to the next (or the one before), past those with `bSkipNextPath`, with the time left over. A pawn's `ViewRotation` follows; the velocity is what it moved. Not done: the points' `GameSpeedModifier`, `FovModifier` and screen flash. |
| Trailer | Through everything. A sprite: at its owner's place plus its `PrePivot` (`bTrailerPrePivot`), or its `Mass` behind the owner along its facing (`bTrailerSameRotation`), or the place. Anything else: at the place, turned as the owner (`bTrailerSameRotation`) or facing away from where the owner's going (straight up when it's still). |
| Climbing | By its acceleration (the scripts': `aForward` x 0.02 up the view's Z, 120 a second), while a ladder is 1.6 radii ahead at the head going up (4 under it) or the feet going down (4 over them). Onto a floor: `Landed` (the scripts walk). At the top with no ladder at the head: 4 units on, and if that's blocked by a ledge (`TestForLedge`), `ChangePhysics(Clambering)`. At the bottom with none at the feet: `ChangePhysics(Falling)` (the scripts let go). Its velocity is what it moved. |
| Hanging | Along the ledge by its acceleration (`aStrafe` x 0.008): a hand's box (`HandCollisionHeight`) swept over the ledge, moved only where there's a hand hold. No ledge under the hands (`TestForHangLedge`): the scripts' `Falling`. |
| Clambering | Up at `DodgeClickTimer` units a second (its scripts' `PlayClambering` sets 100 or 130; Kira's 104 or 130) until the feet are over `LedgeHeight`, feeling 4 units forward; clear there (or 4 over), `Landed` with the normal of what it felt (none if clear), which sets the rise to 0. Then forward at 120 a second until stopped, when `ChangePhysics(Walking)`; its scripts' `AnimEnd` walks too. Blocked rising: `ChangePhysics(Falling)`. |
| RotatingYaw, RotatingPitch | Turning about the one axis by `RotationRate` a second, only with `bFixedRotationDir` (round and round) or `bRotateToDesired` (the shorter way to `DesiredRotation`, then `EndedRotation`). Before, Reluva turned them twice, so ARD_TortureMachine turned at double speed and never backward. |
| Particle | No gravity: its velocity changed by its acceleration (a ship lifting off, debris settled), moved through everything, with no events. One that makes copies stays where its scripts put it. |
| Newtonian | Free flight by momentum. An actor (a thrown drum) keeps its velocity, which only its scripts change, sliding along what it hits. A pawn (the Vulture, the Wraith, the Arachnid): its acceleration, at most `AccelRate`, changes its velocity, up to `AirSpeed` (times `DesiredSpeed` if not a player); no gravity, no friction; its velocity is then what it moved. |
| Fish | As a swimmer: the zone's friction, buoyancy and current, up to `WaterSpeed`, facing the way it swims with no roll. (The engine works out a pull toward its `Destination` too, but keeps it in `LastSeenPos`.) |
| EXP_Trail | An explosion's trail: by its velocity and acceleration, through everything. |
| The rest | Only their copies move (see Effects). |

**Finding a ledge or a ladder**, as Engine.dll does. Each search turns
the player to face into the wall before asking its scripts'
`ChangePhysics`, and puts the rotation back if they refuse.
- **A walk blocked by a wall of the level** (with the move left over):
  - **A ledge** (`TestForLedge`): only head on to an upright wall (the
    move and the view both within -0.85 of it), not crouching, and not
    in a level with `bNoActionLevel` unless climbing.
    - The search: a box up to twice `ArmCollisionHeight` +
      `StandCollisionHeight` (the ducked box from the feet if standing),
      on by twice the class's `CollisionRadius`, and down onto a floor
      (`TestStepUp`).
    - What it tells the scripts: `LedgeHeight` (the feet on it),
      `LedgeNormal` (its floor's normal, not the wall's), `LedgeDepth`
      (how far on, as a share of that), `HeadRoom` (the room over it, as
      a half-height up to `StandCollisionHeight`) and `LedgeItem`.
    - It's taken if higher than a step and deep enough (over 1.6 radii if
      more than 45 high, else 0.5), flat (0.9), with floor just past the
      wall's face (`TestForStrongLedgeFooting`), and with the wall and
      floor under the hands 8 units to either side
      (`TestForHandHold`, which makes it the player's Base).
    - Then `ChangePhysics(Clambering)`, and the walk ends.
  - **Else a ladder** (`CheckClimbing`): a ladder texture (`TT_Ladder`)
    at the chest and the head, 1.6 radii into the wall. The player is put
    against it, 2 units off. Then `ChangePhysics(Climbing)`.
- **In a jump** (only the scripts' reaching jump, with `CollisionHeight`
  at `StandCollisionHeight` + `ArmCollisionHeight`), after each falling
  move toward a wall 6 units ahead at the chest:
  - **A ladder there:** `Climbing`.
  - **Else a ledge for the hands:** found with the hands' box and a
    search of twice `ArmCollisionHeight` + `HandCollisionHeight`, at
    least 4 deep and flat. There must be room to get there, a hang
    ledge over it (`TestForHangLedge`) and a hand hold. Then `Hanging`,
    and the player is put there with the box's top at the ledge.
- **Ducking under** (`TestForLedge`'s other branch, `ForceDuck`): the
  player is standing, not ducking yet, and walking. The ducked box (from
  the feet, `DuckCollisionHeight` + 1 high) can go on where the standing
  one is stopped. Pushing on for over 0.3 s with no step there, the player
  ducks under it. The same happens at a step with less room over it than
  the player's height (and not crouching).
  - `PushToAutoDuckTimer` grows by each walking frame's time, as
    Engine.dll's `physWalking` adds it. It goes back to 0 on a fall or a
    real move (over 10 a second). Reluva had only ever set it to 0.
  - Ducking sets `CollisionHeightOverride` (to the standing height, if
    unset), `ForceDuckLocation` (the place, its feet at the ducked
    height) and `bIsCrouching`. The scripts' walking state then shrinks
    the player and keeps it down until it has moved 20 away.
- **Ladders' edges:** put against a ladder, the player is kept 12 units
  inside its polygon's left and right edges (`GetNodeXOffsets`).
- **Swimming** into a wall tries a ledge and a ladder as walking does
  (`physSwimming` asks the same tests).
- **The hit's node** (`TestHitIntegrity`): Reluva's collision already
  gives each hit its BSP plane's own normal and node, which the
  engine's test exists to make sure of.
- **Seen:**
  - **The ladder:** on M01_KiraL2, walking into the ladder under
    `CTVE_LadderTop2` climbs it at 120 a second, pulls up over the top
    and walks on.
  - **Pulling up:** on M01_SiskoL1A, walking into the ledges at (-362,
    2981) and (-316, 3222), 56 and 60 up, goes through `PlayerClambering`
    onto them.
  - **Refused, as the tests say:** other places found there are walls
    that go on up past reach, or tops with nothing for the hands at
    their edge (`TestForHandHold` finds no wall 2 under the top: the face
    is set back up there).
  - **Hanging:** on M01_KiraL2 at (351, 3516), jumping at a ledge 122 up
    while running at it catches it (`PlayerHanging`, `LedgeHang`). She
    stays when let be, and shuffles along it with the strafe keys
    (`LedgeR`, 48 a second). Forward climbs up after half a second
    (`ledgeu`) and walks on 104 higher; back lets go. Held forward
    through the catch, she pulls up at once, as the hanging state's
    script asks.
  - **Ducking under:** on M03_SiskoL2 at (1515, -1693), walking into a
    low gap stops the player for 0.3 s, then the player crouches
    (`CrchWalkF`, height 32.5) and goes on under it until the ceiling
    slopes too low even for that. Walking back out, the player stands up
    again.
  - **Out of water:** on M01_KiraL1B at (-1589, -5514), swimming into a
    rim 20 over the water floats the player up to it, then
    `PlayerClambering` pulls up onto it. Into a wall with no room over
    the water (M11_SiskoL1a), the player stays swimming.
  - **A ladder's edges:** on TrainingRoom, walking into the 32-wide
    ladder at (32, 1625) 15 off its middle puts the player 4 off it
    (12 inside the edge), and 14 off the other way, 4 off that way.
    Then up at 120 a second, over the top, and on.
  - **A ladder from the water:** on M06_SiskoL1B the ladder at (2816,
    -935) stands in shallow water. Wading into it climbs it. At its top
    the player stays on it: the ladder's own top (-1118) is 2 over the
    floor behind it, so the footing test, 2 under the ledge's top, finds
    no floor (`TestForStrongLedgeFooting`), as Engine.dll's tests give.
  - `reluva-tool ue-ledges <game> <map>` lists places to try (a wall
    straight ahead, a flat top 30 to 110 over the floor to pull up onto,
    or 115 to 160 to catch in a jump), low gaps to duck under, and walls
    to swim into (with the rim's height over the water). `ue-ledges
    <game> <map> ladders` lists the ladder polygons (`TexTag`
    TT_Ladder), their width, and whether their foot is in water.
    `ue-trace` sweeps a box through a level's BSP.

The input axes are 20 times the bindings' speeds (`aBaseY` 6000 for a
held MoveForward), as UE1's input scales a held key's axis. The
climbing speeds above are the scripts' factors of those.
Natives: `Pawn.StopWaiting` (a `Sleep` its state code is in ends),
`Move`, `MoveSmooth`, `SetLocation`, `SetPhysics`, `Trace`
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
- **Matched against Engine.dll** (`APawn::findPathToward`,
  `breadthPathFrom`, `pointReachable`, `actorReachable`,
  `LineOfSightTo`, the path iterators): Reluva's own code, written from
  what the routines were read to do.
- **The order of the points** is the level's: LevelInfo's
  `NavigationPointList` and each point's `nextNavigationPoint`, as saved
  with the map; the iterators and the searches follow it, so ties go the
  same way.
- **Reaching a place** (`pointReachable`, and what the search asks):
  within 800 across, not water the pawn can't swim, seen from its eyes
  (`BaseEyeHeight`) unless already known, the place nudged to where the
  pawn fits. Then a walk test: steps of the collision radius (at least
  128) along the floor, each a test move (as `MoveActor`, without events)
  up a step when blocked and down onto a floor (0.7 or flatter) within
  `MaxStepHeight` + 2; a fall, too steep a climb or 101 steps and it
  can't. Flyers and swimmers test a straight move. The pawn is put back.
- **Reaching an actor** (`actorReachable`): The Fallen's native takes a
  navigation point (or an item's marker) by the network alone when the
  pawn is at a point and fits the level's biggest; else the engine's
  test: within 800 (a pawn at any range), seen unless known, near enough
  by both sizes.
- **`FindPathToward(Actor, bSinglePath, bClearPaths)`, `FindPathTo(Vector,
  ...)`:** `bClearPaths` (on unless told otherwise) first sets each
  point's cost back (its `ExtraCost`, or what its `SpecialCost` event
  says). Then **one** start point: of the nearest 32 within 800, the
  nearest the pawn sees and can reach (or the one it stands on); a
  falling pawn looks from where it will land. **One** end point: of
  those near the goal, the nearest it can be reached from. The search
  goes back from the end over each point's `upstreamPaths` (the reaches
  into it) the pawn fits (its size in whole units; walking, flying,
  swimming, jumping, doors, special and player-only against its
  abilities) and a mover doesn't bar (one it can't open), cheapest first,
  each step costing its length and the point's cost, until a start is
  reached. A start further along (nearer the goal, or behind the pawn
  against the way it was going) is preferred over the nearest. The way
  goes in `RouteCache`; a point with its own handling (a lift, a door)
  names where to go instead (`SpecialHandling`, the pawn's
  `SpecialGoal`). None if there's no way.
- **The iterators:** `AllPathNodes` (by class and tag, in the level's
  order); `RadiusPathNodes` and `RadiusVisiblePathNodes`: of the **8**
  nearest points within the radius (the engine keeps one nearest-points
  list for everyone, reused while asked near where it was made), nearest
  first, those of the class, and for the visible one those that can see
  the place.
- **`LineOfSightTo(Other)`:** from the eyes. Its enemy: its middle.
  Within 1000: its chest (0.8 of its height up) and, for a pawn within
  500, its sides. Further, a pawn's middle only, and for the AI only
  half the time and not past half its sight range.
- **In play:** over all maps' first 20 seconds, 1,620 ways were straight, 2,598 along paths and 1,460 none. Many of the
  failures are faithful: on M05_WorfL2 the patrol points a guard is sent
  to are an island in the map's own data (no reach in or out either
  way), so the original fails there too.
- **Not done:** jumping and dropping reaches (none of The Fallen's maps
  has them), and the editor's path building (the maps' own reaches are
  used).

### Dialogue

`vm_dialog.cpp` plays the characters' lines from the game's dialogue
tables ([dialogue.md](../formats/unreal/dialogue.md)) as Engine.dll's
dialogue engine (`FDialogEngine`, kept by the level) does, read from it;
the code is Reluva's own.
- **Two tables a level:** the general one (`GEN_Dialogue`) and its own:
  LevelInfo's `DialogFile`, else the one its map's mission names
  (M01_KiraL1A: `M01_Dialogue`). The menu's Entry has none. Their header
  rows give each type's priority (`Types`: 1 DIE, 2 CIN, 3 the hails and
  TRN, 4 PAN EXB EXG GSP DWN, 5 FSE, 6 SUR TTP GPI KIL FHE EVT HUD TBD,
  7 BAT LSE INT LED ACK, 8 AMB REP) and the faces' order (`Faces`).
- **Keys:** each line is known by TYPE, SPEAKER, LISTENER (`___` for
  none), LINE ID and MISSION run together (`HBOOBNKRA010K03`). A key
  asked for fits when each of its letters is `?`, or the line's is `_`
  (a line to no one fits any listener), or they're the same. NEXTLINE
  and RETURNLINE (MISSION TYPE SPEAKER ID in the tables) are keys so,
  found at load when only one line fits.
- **Running on:** in the tables' order (the general one, then the
  level's), a line runs on to the next when it's of the same type and
  the next number (Kira's log on the Promenade, 010 to 011; the opening's
  conversation, whoever speaks). That's its next line, whatever its
  NEXTLINE says, and such a line is never picked to start one.
- **Defaults:** the player's hail option (HOP) with no NEXTLINE goes on
  to the crewmate's answer (HBO from the listener to the speaker), with
  no RETURNLINE back to the player's sign-off (HCL); an answer to someone
  with no NEXTLINE to that someone's sign-off.
- **`FindBestDialogLine(out Index, Type, FilterTag, ListenerTag,
  SpeakerTag, LineID)`:** the key is the type, the speaker (asked, else
  the caller's own `Speaker_Tag`), the listener and the number, `?`s
  where not asked. Only a type and no tag finds nothing. The mission is
  never part of it. Of the lines it fits (not those that run on):
  - just one is the answer as it is;
  - else each is scored. Asked for by tag, only lines with that tag;
    otherwise a tagged line only for the caller whose `Dialogue_Tag` it
    is (+100,000). For whoever the player plays: a cut scene's always, a
    G mission's, one of the player's letter (K, S, W), an M mission's for
    Sisko and Worf (never Kira). Its condition met (+1,000,000). 10,000
    off for each hundred of its number. The times it's been said off (a
    pain cry's: a random 0 to 14 instead). The highest wins; of equals,
    the later in key order.
  - It returns how many fit, not how many would do.
- **Conditions** (`Conditional`): brackets and commas are spaces; words
  gather into terms between `AND` and `OR`, taken left to right (a 0
  before AND gives 0, a 1 before OR gives 1).
  - A term with a `.` in it is about a variable (`Class.Property op
    value`, of the caller), any other an event of the dialogue input's,
    by a fixed list: `ObjectiveCompleted` (1 to 9), `PlayerFaceEnemy`
    (the caller's enemy), `EnemyIsA`, `FriendClose`, `LowHealth`,
    `HasItem`, `SpecialCondition`, `PlayerIs` (`IsPlayer`), `InPlace`
    (`PlayerInPlace`), `DS9Phase` (`OpsLevel`)... A `!` anywhere in the
    term turns it round.
  - A name not on the list, or arguments missing, makes the condition
    one the engine can't test: such a line is never picked, and
    `TestCondition` reads it as met (and logs "Test Failed").
  - `LowAmmo( 0.2 )` has a `.`, so the engine takes it for a variable
    and it never holds: the six hail options that ask for ammunition
    when low are never picked, and the beam-down below can't happen as
    it did before in Reluva. The original game's log would show it.
- **`IsDialogLineSpecial`:** a line never said yet, numbered under 800,
  and with a tag longer than a speaker's code or a condition. The HUD
  shows that a crewmate has something to say when hailing them would
  find one (`CheckSpecialComm`).
- **`StartDialog`** (and `PlayDialog`, which calls it): a hail (HAL)
  with no tag is the player's options to whom it hails (HOP), asked as
  the player; one asked by tag that finds nothing is tried as HOP. A new
  hail cuts off any hail line playing. It returns a thread number (0 if
  no line would do), and `IsDialogThreadPlaying` stays true while a line
  of it is said or waits.
- **A line starting** (`StartLine`):
  - A line for another character than the player's is swapped for the
    same line of the player's letter (then M, for Sisko and Worf), or
    not said.
  - Who says it: the caller, if it's the line's speaker; else (and for a
    voice-over or communicator line, V or H) whom the player's
    `GetSpeakerActor` names for its code.
  - **One line a speaker:** busy with a more important line, it refuses
    the new one; with a less important one (or the same type, or a HUD
    line) that one's cut off (no event, its thread ends); else the new
    one waits its turn, and lines waiting for that speaker that matter
    less are dropped.
  - **Its voice** through the speaker's `PlaySpeech` (at 0.65; 1.4 for
    deaths, pain, first sight and battle cries; 0.3 for EXG), which
    plays it on the talk slot within 1100 units, high-passed on the
    communicator's picture. The voice moves the speaker's mouth (Sound:
    lip-sync).
  - Said by a pawn: **the listener** (none for a hail; else of the pawns
    within 800 of the speaker, one with the line's listener code, else
    the nearest, the player counted at a fifth of its distance; in a cut
    scene only one with the code). **The level's** `Talker`,
    `Listener`, `CurrentLineType`, `CurrentLineIndex` and
    `CurrentLineFlags` (the CAMERA column's flags) if no line has them,
    or this one is of a more important type, or as important and said
    nearer the camera. Its animations (`PlayDialogAnim` at a tween of
    0.3) while the pawn is `Frozen`. Its face (`FacialExpression`, its
    place among the tables' faces; `FacialTimer` to half a second after
    it ends). `HudText` for its voice's length and pause. A note in
    brackets is heard only.
  - **How long:** the frame's time, its voice's length (3.5 seconds
    without one), its pause and a quarter second; a HUD line's clock runs
    while paused, the others' don't.
- **A line ending:** the level's fields cleared if they were its, its
  voice stopped. Then its EVENT (words apart by spaces: a tag to
  trigger, `DoEvent`, or with a `-` before it one of the HUD's
  beam-downs, `DoSpecialEvent`): so a line cut off never sets its event
  off. Then its thread goes on: its next line (found then, for no
  caller, if the tables didn't name just one), else the RETURNLINE its
  thread holds (not a sign-off after a sign-off), else the thread ends.
  A speaker destroyed ends its thread.
- **Hails** on M06_WorfL1A: "Captain, what is your status?", Sisko's
  report ("Heading to the objective. Neutralizing hostiles."),
  "Acknowledged. Worf out."
- **The CAMERA column's flags**: `V:1` a voice-over (a log), `H:1` heard
  over the communicator. They become `Level.CurrentLineFlags` 16 and 32
  (Pawn's head tracking then doesn't turn to the talker), and an H line's
  speaker is the HUD's picture of them (`GetSpeakerActor`'s
  `bForceUseHudMeshActor`). A line's place and turn (`A:`, `R:`) are the
  scripts' to ask for (`GetDialogueCameraData`, the cut scenes'
  directors and speech triggers).
- **The AI's own lines**, which no script asks for: the dialogue input's
  native `DialogTick`, which its `Tick` calls each tick while the HUD
  shows no letterbox. Reluva does as Engine.dll's does. It does nothing if
  the HUD's `IsLetterBoxOn` says so, or the player's `PlayerStart` has
  `bNoAutoTriggerDialog`. Otherwise it goes through every `CTVE_Pawn` that
  isn't the player and has a speaker code. Each pawn's state is kept in
  its `DLG_` variables.
  - **Gates:** more than 0.5 seconds since it last spoke
    (`DLG_LastSpeakTime`), not `bForceStasis`, and within 800 of the
    player. (The engine also wants the pawn's zone drawn in the last 5
    seconds; Reluva doesn't keep that.) On a level with `bNoActionLevel`,
    only TTP is possible.
  - **The type, first that fits** (a type it has no lines of at all is
    marked in `DLG_Flags` and never tried again):
    - DIE: the tick it dies.
    - PAN: hurt this tick. 70% of the time; otherwise a breath
      (`PlayFoleySound` 15) instead.
    - FSE: a new enemy that's alive and `IsActorAnEnemy`, unless the
      pawn is a friend and its enemy the player.
    - LSE: its enemy gone while still alive, with the same exception.
    - KIL: the player died with this pawn as their enemy. Once: it sets
      `DLG_Command` to KIL.
    - FHE and GPI: when its `DLG_Command` becomes that. The Vorta's and
      others' card-giving sets GPI.
    - The rest only after 4 seconds since a try that failed
      (`DLG_LastAttemptedSpeakTime`):
      - LED: while it's `bIsLeading`.
      - SUR: while the dialogue input's `PawnSurrenderOrPray` says so (in
        `Cowering` or `Surrender`).
      - Both of those within 300 of the player, at random about once
        every 4 seconds, once it's been 4 seconds quiet. Nothing else is
        said while either holds.
      - TTP: no enemy; the player alive, within 200 and not faster than
        200; the pawn facing the player (within about 45°, or 72° inside
        75 units). On a no-action level the player must be looking at
        the pawn too.
      - Else, not on a no-action level:
        - AMB: at random about once every 15 seconds (5 with a
          `Dialogue_Tag`), once it's been 15 quiet. Only with no live
          enemy, or as a friend its enemy isn't after.
        - Else, about once every 4 seconds: BAT while `bIsAttacking`
          against a live enemy, or INT while playing
          `InterrogateBajoran`.
        - Else REP: about once every 5 seconds, after 5 quiet, with no
          live enemy as for AMB.
  - **Saying it:**
    - Beyond (1.2 − `ObstructionFactor`) × 800 the player must see the
      pawn, else the try fails.
    - The line is its own (its speaker code; a tagged line only for the
      actor with that `Dialogue_Tag`, as a Vorta's watch-station report is
      that Vorta's), the start of what it says, its conditions about it.
    - A TTP isn't said again: not the same line, nor a general one after
      one (`DLG_LastSpokenTTP`). Nor while a hail is on.
    - Said, it sets `DLG_LastSpeakTime` and `DLG_LastSpokenLine`.
    - Then it goes through `StartLine`'s rules for a busy speaker, as
      any line does.
  - **Every tick**, for every pawn within the gates or not, its
    `DLG_OldHealth`, `DLG_OldEnemy` and `DLG_OldCommand` are what it has
    now. So a change while it's far off is never voiced.
  - Over all maps' first 3 seconds the AI says 2 lines, where Reluva's
    own earlier reading (1500 units, its own timers) had 9.
- Over all maps' first 20 seconds, 30 dialogues start (66 lines said)
  and the AI says 11 lines of its own. FindBestDialogLine finds no line
  130 times: mostly scripts asking whether a pawn has a line to say.

### The HUD

The engine hands the player a Canvas object each frame, before the world
is drawn (`PreRender`) and after (`PostRender`), and the player hands it
to its HUD. `vm_canvas.cpp` does the same: the Maps page asks for it once
the world's drawn (`ScriptLevel::renderHud`). The canvas is made ready
first: `Reset`, its size and clip the screen's, its origin and pen at 0.
What the Canvas natives draw is kept as a list for the page:
- **tiles:** a texture's rectangle (in texels) at a rectangle of the
  screen (in pixels), with a colour (`DrawColor`) and a style (`Style`:
  normal, masked, translucent, modulated);
- **actors:** drawn over what's there, each in the view (the frame,
  below) it was asked in, and in its order among the tiles.

The natives:
- **`DrawTile(Tex, XL, YL, U, V, UL, VL)`** at the pen (`OrgX + CurX`,
  `OrgY + CurY`); the pen moves on by its width. `DrawTileClipped` is cut
  to the clip (`OrgX` to `OrgX + ClipX`...), its texture rectangle cut
  with it. Canvas's own `DrawIcon`, `DrawRect` and `DrawPattern` are
  script, on top of these.
- **`DrawText(Text, CR)`** draws at the pen in the canvas's font
  ([font.md](../formats/unreal/font.md)), a tile a letter. Each letter's
  width plus `SpaceX` moves it on, and the tallest letter plus `SpaceY`
  makes a line. The text wraps between words at the clip's right edge,
  each line centred in the clip with `bCenter`; a wrapped line starts
  back at the pen's column, not the clip's edge. The pen moves past it
  (with CR, to the next line's start), and `CurYL` keeps the line's height.
- **`DrawTextClipped`** draws one line, cut to the clip, with '&' (a
  menu's hot key) left out if asked. `DrawScaledText` scales the letters.
- **`StrLen`** measures text wrapped as `DrawText` would; `TextSize` on
  one line.
- **`SetRenderFrame(X, Y, W, H, Location, Rotation, FOV, ClearZ)`** sets
  the frame the next actors are drawn in: a rectangle of the screen and a
  camera, as given (Engine.dll's `execSetRenderFrame` hands them to
  `FSceneNode::ComputeRenderCoords`), with the field of view given or 90.
  It also sets the viewing player's `FovAngle` to that field of view, and
  clears the depth with ClearZ. It lasts until the next one or the end of
  the frame; each frame starts in the world's view. The HUD's
  `ResetCanvas` sets the whole screen with the HUD actor's own place (the
  camera's, `UpdateLocation`) and rotation, and the `FovAngle` that
  `SaveCanvas` kept. The scripts draw no actor in it after that and
  project nothing in it, so it shows nothing. What it does is put back
  the `FovAngle` that a portrait's or the tricorder's frame changed,
  which `CTV_Hud`'s debug labels read.
- **Tiles in a frame** are placed from its corner and cut to it, and the
  canvas's clip (`ClipX`, `ClipY`) becomes its size, as the scripts
  expect. The info pad's documents are drawn from 0, 0 and scroll
  inside their window. The star map's blocks scroll inside theirs, scaled
  by the clip the HUD saved before setting the frame (`saveClipX`). The
  tricorder takes `GetScreenCoords` as a share of `ClipX`.
- **`DrawActor`, `DrawClippedActor`** draw the actor where it is, in the
  current frame (`DrawClippedActor` in its own rectangle, the frame's
  camera), the depth cleared first if asked; the page draws it, hidden or
  not. Its draw type, style and glow are taken as they are at the call:
  the HUD's speaker portrait (`DS9_Hud_Mesh_Actor`) sets them only around
  it. The HUD uses these for the communicator's portrait (the speaker's
  head, a camera 78 units in front of it at 10°), the tricorder's dots
  and markers, beacons and the beam-down cloud.
- **`GetScreenCoords(Location, X, Y, Scale)`** is where a place falls in
  the current frame's view, from the frame's corner as tiles are placed;
  false behind it.
- `DrawPortal` isn't drawn: no script calls it.

The Fallen's HUD (`DS9_Hud`) is built from textures that draw themselves
(`Texture.Draw(Canvas)`, script). Each HUD texture keeps where it goes in
properties it borrows:
- `Specular` and `Friction`: its place, in 800 × 600 units;
- `PrimeCount`: the corner or edge that place is from;
- `Alpha`: its style.

The HUD's animated textures are FliTextures ([texture.md](../formats/unreal/texture.md)),
played by script (`DS9_Hud.SetFlick`): `Mode` 0 stops, 1 plays once, 2
loops. Once a FliTexture is drawn the level plays it each tick: its
`FrameTime` grows by the tick times `Rate`, and `FrameNum` moves on a
frame each 1/30 s of that. Played once, it stops back on frame 1 with
`Mode` 0, which `FlickPlaying` waits for. A tile draws the frame
`FrameNum` says (the communicator's bar sliding open, the meters).

That's why textures need their own stored properties (above): with the
defaults, every piece of the HUD was off the screen.

On the Maps page, while playing, the tiles are drawn over the world in
screen pixels:
- see-through texels stay so in every style, as letters are drawn masked
  however they blend;
- translucent tiles are added, modulated ones multiplied in.

On M04_KiraL1 the HUD shows:
- the medical panel bottom left: its meter and Kira's health (100) in the
  HUD's number font;
- the weapon panel bottom right: the phaser rifle's picture and its
  ammunition (150);
- F1, the communicator: the Starfleet badge and the crew list (O'Brien,
  Dax, Bashir) with its selection marker.
- F1, then Fire: hails the selected crewmate. The communicator's bar
  slides open and the answer is written in its box beside their face
  (O'Brien: "You'll need to reinitialize the EPS taps...").

The keys:
- **Passed to the game** while playing: F1 to F12 and the number keys,
  as the game binds them, on press and release.
- **Aliases** are followed: `ShowObjectives` is `Button bShowObjectives`.
- **A `Button`** holds the player's input flag while the key is down.
- **An exec function** is called as the key goes down, with its argument
  if it has one (`SwitchWeapon 2`).

With the game's HUD drawn, the page leaves the objectives and subtitles
to it and puts its status line between the HUD's panels.

### The menus

The Fallen's menus are the HUD's own, drawn on its canvas, and UWindow's
options windows, drawn by the console (`UMenu.DS9_Console`).

Keys reach the console first, as the engine hands them on
(`LevelPlay::consoleKey`, the console's `KeyEvent` with the key's
`EInputKey` code: 13 Enter, 27 Escape, 37 to 40 the arrows). The
Fallen's console passes the arrows and Enter to the HUD
(`GetKeyEvent`), which acts on them while a menu shows. What the console
doesn't take goes on to the bindings: Escape is `ShowMenu`. While the
menu shows, the Maps page leaves the arrows and Enter to the game
whatever `KeyEvent` answers, as the HUD's answers false having acted on
Enter. The page's own Escape (leaving) waits until play stops. The keys
held are kept for `IsKeyDown`.

`ShowMenu` opens the info pad and pauses (`SetPause`: `Level.Pauser`
set). Paused, time stands still: only the player's `PlayerTick` and the
actors with `bAlwaysTick` run (the HUD, whose `Tick` runs the menu). The
FLC textures and the console still tick, the dialogue doesn't.

On M04_KiraL1:
- **The info pad:** Resume Game, Mission Objectives, Tactical Data,
  Communications Log, Retrieved Files, Main Menu. Mission Objectives
  lists them and shows the selected one's document, its text and
  picture, in the pad's lower window.
- **The main menu:** New Game, Load Game, Save Game, Options, Quit, with
  the star map scrolling in its window above. New Game asks the
  character (Sisko, Kira, Worf), then the game or the training mission,
  then confirms. Saying yes fades out and travels (`ClientTravel`
  "CM01a_SW/CM01_A_Sisko01"), and the page loads that map and plays it
  from the start the portal tags: the opening cut scene.

The game's own start is `Title.dsm` (Default.ini's `LocalMap`): the
publishers' logos, a cut scene. Escape during a cut scene fast-forwards
it (`FastForward`): the page runs the level as fast as it can, up to a
twentieth of a second's work a frame, until the game turns it off when
the letterbox goes. Then `CM_SpaceStation/intro`, whose start tagged
`Intro` makes it the intro level (`bIntroLevel`). The credits roll over
the station fly-by, and Escape skips them to the main menu, with no Save
Game.

Options opens UWindow (the console's `Menuing` state, then `UWindow`):
the engine calls the console's `PostRender` after the player's, and its
`Tick` every tick, paused or not, and the windows are all script, drawn
through the same canvas. The options window has Game, Video, Audio,
Controls and Input pages. While it's open the mouse is the windows': the
page puts the console's `MouseX`, `MouseY` where its pointer is, and its
buttons are keys 1 and 2. Escape closes the window.

Console commands (`ConsoleCommand`, `vm_console.cpp`):
- `get Section Key` gives a setting (`ini:Engine.Engine.ViewportManager
  Brightness`: the section of the class the ini names), else a class's
  default; `get ... Class` the class it names.
  - The render device (`get ini:Engine.Engine.GameRenderDevice Class`)
    is Direct3D's (`D3DDrv.D3DRenderDevice`) where the `.ini` names the
    software one, as the shipped `Default.ini` and `Unreal.ini` do.
    Reluva draws as the hardware drivers do. The scripts ask for it
    (`Actor.InSoftwareMode`) to hide the wrist flashlight, the breather,
    the armour belt and the drill's inductor, and the tricorder grid, in
    software mode. Until November 2026 they were all hidden. Now the
    flashlight shows on the raised forearm (`C3DAttach(Player, 'Wrist',
    'Arm')`), the breather at the mouth ('Face'), and the video options
    name "Direct3D Support".
  - Picking items up: standing still within 52 of one (`ScanForItemsToPickup`),
    the player turns to it and plays the pick-up (crouching to one under
    the feet), then takes it. M05_SiskoL1a's breather at (3205, -2204)
    is in a nook open only to +Y, and is taken from there.
- `set` changes one (above), and the class's property in its defaults and
  every object of it.
- `KEYNAME n`, `KEYBINDING Key` and `SET Input Key Command` read and change
  the bindings. The controls page lists them ("LEFTMOUSE OR CTRL"), and
  the page's keys follow what it sets.
- `GetCurrentRes`, `GetRes` give the page's size; the colour depth is 32.
- The world and skin detail (`[WinDrv.WindowsClient]` `TextureDetail`,
  `SkinDetail`) are the engine's own settings, which no shipped `.ini`
  holds. They read as High, the first of the game's choices (Reluva's
  reading).
- `SaveGame [QuickSave]` saves the level (below).
- `Exit` stops playing.
- The rest (sound, detail settings) do nothing yet.

The mouse in the HUD's own menus: its buttons, as the game binds them.
The left (`Fire`) selects, as Enter does, and the right (`AltFire`)
steps the choice on. Its movement does nothing: the HUD's
`HandleMenuMouse` is compiled to return at once.

On the intro level the player's health is 94: its `Intro` start is 410
units above the floor, the player falls hidden from it, and the landing
costs 6 (`TakeFallingDamage`, past −850 a second). Nothing at that start
stops the fall, so this is the game's own doing; the menu hides it, and
New Game starts a new player.

### Sound

`vm_sound.cpp` takes what the scripts play, and the page plays it
(`level_audio.cpp`, through `common/audio`'s mixer). How is Engine.dll's
(`execPlaySound`, `CheckHearSound`) and the game's audio driver's
(Miles.dll, The Fallen's `MilesAudioSubsystem`: its `PlaySound` and
per-frame `Update`), as read from them; the code is Reluva's own.

- **`PlaySound(Sound, Slot, Volume, bNoOverride, Radius, Pitch,
  bNoHearNoise)`** and `PlayOwnedSound`:
  - Left out: the slot is `SLOT_Misc`; `Volume` and `Pitch` are the
    actor's `TransientSound` ones, `Radius` its `TransientSoundRadius`.
    A `Radius` of 0 is 1600 (`SoundRadius` is the ambient sound's only).
  - **Who hears it** (`CheckHearSound`): the player, from its view target,
    else its camera. Beyond `Radius` / √1.3 it isn't played at all. With
    the level's walls in the way (not for a `SpecialEvent` or `Keypoint`)
    it's at 0.35 of its volume for good, and unless the player set it off
    (its `Instigator`) heard only within √0.6 of that.
  - **Slots:** an actor's slot (all but none) holds one sound. One played
    with `bNoOverride` keeps it: a new one isn't played. Otherwise a new
    one takes it.
  - **Channels:** 16 (the .ini's `Channels`). With all taken, a new sound
    takes the least important one's if that's no more important than it:
    importance is its volume × (1 − distance / radius), at least 0.0001
    of it; else it isn't played.
  - **Loudness:** its volume × (1 − distance / `Radius`), in hundredths of
    the driver's 127, at most 100; nothing beyond `Radius`. The SFX or
    speech volume (a sound named `DIALOG`) and the master go over it.
  - **Side:** the angle off where the camera faces (one behind heard as
    if in front), less within a tenth of the radius, as Miles' 0..127.
  - **Following:** it's heard from where its actor is; one destroyed is
    heard where it was.
  - **Looping:** a sound whose name has `LOOPING` (the `Looping` groups)
    goes round until something takes its slot.
  - **The actor's sound filter** (The Fallen's `LowPass` and `HighPass`)
    goes with it. The driver mixes a filtered sound flat, and then the
    level's walls between it and the camera take its `ObstructionFactor`
    off (Pawn 0.4, CTVE_Pawn 0.7). The others go through Miles'
    positional mixing, whose obstruction does nothing without EAX. The
    filters themselves (a voice on the communicator is high-passed at
    600 Hz) aren't applied: the mixer has no high-pass.
  - **The AI hears it** unless `bNoHearNoise` (`PlayOwnedSound` never):
    each pawn but the players, the actor and its owner, if the actor has
    an owner and it or its owner is a player, a weapon or a projectile.
    Heard at 1 − (distance / `Radius`)², with 0.65 off through walls,
    if that's above 0 and above 1 − `HearingThreshold`: `HearNoise`. A
    CTVE_Pawn whose enemy isn't the actor first gets `DLG_Command`
    `FHE`. `PlayDummySound(Volume, Radius)` is that noise alone.
- **Ambient sounds:** each actor's `AmbientSound` with a `SoundVolume`
  above 0. It starts when the camera comes within 25 × (`SoundRadius` +
  1) units and stops when it leaves (starting afresh on coming back).
  Its volume is 2 × `SoundVolume` × `AmbientFactor` (0.7) / 255; a
  Keypoint's (the `AmbientSound` actors) times the level's `AmbientBus`
  / 128; a light's times its `LightBrightness` / 255. Its pitch is
  `SoundPitch` / 64. Then loudness and side as a played sound's.
- **Music** is a sound whose name has `MUSIC`, or a streamed one (the
  MP3s in `mp3/`, [sound.md](../formats/unreal/sound.md)). It's heard
  alike everywhere. A stream is at the music volume, twice as loud as
  the others before it (`MusicVolume` 200 of 255 puts a track at full).
  - The player's `CurrentMusic` names the track. The start sets it
    (`OpeningSoundTrack`).
  - A music trigger (`CTVE_MusicEvent`) plays a track through the HUD's
    `PlaySound`. Played while it's playing, it goes on. Another starts at
    once, at full volume, where the HUD's `StartTimeOffset` says, while
    the old one fades out over 3 seconds.
  - Setting `CurrentMusic` to None fades it out over 3 seconds.
  - When a track ends, `CurrentMusic` is None again. The filler music
    (`CTVE_FillerMusic`) waits for that, then plays its own as an
    ambient sound: heard alike anywhere in its range, faded in and out
    over 3 seconds, and not started while a track plays.
  - A loaded game starts its music again.
- **Voices:** a dialogue line's Sound (its record, in the package of its
  table's name) plays through the speaker's `PlaySpeech` (above,
  Dialogue): on its talk slot, as far as 1100 units, at the speech
  volume (its package's name has `DIALOG`).
- **Lip-sync** is the audio driver's (Miles.dll), for any sound an
  actor plays whose WAV has a `phnm` chunk (every voice of the dialogue
  tables: [dialogue.md](../formats/unreal/dialogue.md)), not only
  dialogue:
  - The sound's clock is the level's time (`TimeSeconds`: stopped while
    paused, slowed by `TimeDilation`) from the driver's last update, less
    the actor's `StartTimeOffset`. Each update looks at the window from
    the last update's time to now, the start in and the end out.
  - Walking the sound's words in order, the first that gives something
    is the one, so at most one shape a sound an update and the earliest
    (the rest of a fast frame's phonemes are skipped). A word ending in
    the window is silence, (0, 1), unless the next word's first phoneme
    starts exactly where it ends (words run together). Else a word the
    window's start or end is in gives its first phoneme starting in the
    window.
  - The phoneme's IPA letter gives `C3DSpeechEvent(shape, weight)` to
    the actor that played it: 1 for i and ɪ (0.2), æ ɑ ɔ ə ʌ (0.8), e ɚ ɛ
    (1); 2 u w ʊ; 3 b m p; 4 f v; 5 s z ð ʃ ʒ ʤ ʧ θ; 6 d g h j k l n t
    ŋ ɫ ɹ ɾ; 7 o. Others give nothing, though they use the update up: the
    voices' r (ɻ, 4,252 of their 73,513 phonemes), h (ɦ, 779) and g (ɡ,
    553) never move the mouth, and nothing in them is e or o.
  - When the sound stops (it ends, a new one takes its slot, a line is
    cut off), (0, 0); the update it ends in gives nothing else.
  - The script side (`Pawn.C3DSpeechEvent`) tweens the face's `Phonemes`
    channel to the shape's frame (shape × 0.033 seconds) over a tenth of
    a second, and now and then nods or tilts the head while the weight
    isn't 0.
- **`GetSoundDuration(Sound)`** is its length; `CTVE_Dialog` times its
  lines by it.
- **Footsteps** go by the floor's material: `GetSurfaceTexture(Start,
  End)` is the texture of the level's surface a line meets (`TexMaterial`:
  cement, carpet, metal, water...). The surface is the node whose plane
  the trace stopped at, or the coplanar one whose polygon holds the
  point.
- **The volumes** are the game's settings (`[Engine.Engine]
  AudioDevice`'s section: Miles, in Unreal.ini `MasterVolume=160`,
  `SFXVolume=160`, `MusicVolume=200`, `SpeechVolume=160`, each out of
  255). Each kind's is under the master's, and the options page changes
  them as it goes. So do `UseStereo` and `ReverseStereo`.
  `SetMasterVolume` sets the master for now (a level fading out), and
  `KillSound` stops every sound.
- **Not done:** the driver's Doppler (at its `DopplerSpeed` of 35,000 a
  change of about 1%), the zones' reverb (Miles' room types, by the
  camera's zone's `ZoneRoomType`; under water room 22), the high- and
  low-pass filters, a WAV's own loop points, and the AI not hearing a
  sound played from a `simulated` function.

On M04_KiraL1, in the first seconds:
- the vents' and engines' hums and the lights' buzz (37 ambient sounds);
- the lift, footsteps on cement, carpet and metal, the consoles'
  telemetry, the communicator's chirp and Kira's line.

M01_KiraL1A opens with its music ("evil"), and CM01a_SW, the Defiant
at warp, with the tragic cue.

`ue-play` mixes offline (`wav <file>`: what was heard since the last,
and how loud each second; `sounds`: what's playing, and the last
sounds started).

### Effects

The Fallen draws most effects as copies of one actor, made by its scripts
(`CTVE_MultiMesh`: `NewMultiMesh` puts one on the actor's `MultiMesh`
list, `Delete` moves it to the list's end as spare). `vm_effects.cpp`
moves them and works the effects' natives. All of this is Reluva's
reading of what the scripts set and expect; the original code isn't
known.

- **Copies**: each tick, as the actor's physics mode has them (below).
  Most move by their `Velocity`, which their `Acceleration` changes,
  and are let go when their `LifeSpan` runs out (none: they last). An actor with a copy list is drawn as its copies only (even none
  yet), unless it asks to be drawn as well (`bMultiMeshRenderInitial`).
  Each copy is drawn as the actor's mesh or sprite, in its place, with
  its rotation, `DrawScale`, `AxisScale`, `ScaleGlow`, style and
  texture. `AxisScale` goes along the mesh's axes before its
  `RotOrigin` turns them, as a mesh's own Scale does: the transporter's
  strips (`CTV_Quad`, 800 long in X and turned a quarter by RotOrigin)
  are narrowed to a tenth of their length and grown upwards. An
  imported mesh's turn into the game's axes comes before it: the
  scripts scale their beams' sections along X as they're drawn.
- **A mesh with no skin of its own** (no MultiSkins, Skin, or texture
  in the mesh) takes the actor's `Texture`, as UE1 does: the
  transporter's strips (`Texture = StripTexture`), impact flares.
  Before, they were drawn plain white: big white slabs over a beam-down,
  a white square where a rifle shot hit.
- **Copies by physics mode** (`moveMultiMeshes`), as Engine.dll's
  `phys*` functions for The Fallen's modes do it (read November 2026).
  Each walks the copies from the base's `Next` itself. Most fade only
  in a copy's last second (or half second) of `LifeSpan`, not over its
  whole life. Some count a copy's life down only while it has one;
  the explosions', the fire's and the plasma jet's count it down
  always, so a copy given none is gone on its first tick.
  - Left alone: beams (`BeamSustain`).
  - Spark (an arc's segments): laid on the arc's curve again when it's
    updated (the base copy holds it, below), then every tick each jolted
    a random way by its `Use1` (the two ends `Use2`), as bright as the
    arc, turned to the one before and stretched to it; `TipLocation` the
    last.
  - Particle: pushed by the generator's `Forces` and `WindForce`
    (whatever `bIsWindy` says, and on top of the `Acceleration` its
    scripts set), slowed by `DeltaSpeed`, growing by `DeltaSize`; in
    its last second shrinking (`bFadeSize`) and fading to nothing from
    `MaxScaleGlow` (`bFadeGlow`). Without a generator both fades.
  - Smoke: pushed by `Forces` and wind, fading in its last second
    (`ScaleGlow` = what's left). It doesn't grow.
  - SteamJet: out and slowing, growing by `DeltaSize`; with a second
    left, blown off (30 x a random way plus the jet's
    `MMEventLocation`, back along it) and fading over half a second
    from twice `MaxScaleGlow`.
  - HotSpark: facing the way it flies, kicked somewhere new 3 ticks in
    10 (500 a random way, falling; 200 out), flickering, fading in its
    last half second.
  - CTVE_Sparks: flickering in size (round its `Phase`, the size it
    was given) and brightness, jittered, dragged across to 100 a second,
    each leaving a trail of embers (a quarter its size, up to 0.6 s,
    Style translucent) every 0.01 to 0.03 s.
  - Bubble: rising at the speed it started with, the drift damped,
    shrinking and dimming in its last second (half bright before). At
    the surface (the generator's `SeedLocation.Z`) it becomes a pop (the
    generator's `SpecialTexture`) for 0.08 s, and the generator's
    `bMMEvent` is raised there; with no pop texture it's just gone.
  - GasCloudM4, BeamFadeTail: a life of a second or more is spent at
    once (the engine counts it down again until it's under a second);
    then size and brightness go down 0.2 a second.
  - FanParticle: along its line (`Phase`, slower in the middle) at the
    generator's `Acceleration.Z`, flaring out toward both ends (`Vect1`
    x (1 + 4 Phase^2) + `Vect2` x Phase), fading in the outer halves.
  - SpiralJet: out along the axis (its `Phase` the distance), round a
    circle widening with it at a turn a second, growing; fading in its
    last second.
  - PipePath: drawn toward its `Acceleration` (a point), pushed away
    from it at 1.5 to 2 and 3.5 to 4 s left, damped; started again at
    its end (`Vect1`, `Vect2`, `Use1`).
  - SwarmObject: by velocity and acceleration. With no textures it
    faces the way it flies; otherwise its texture steps a frame a tick
    through the generator's `AnimTexture` (the fireflies' 8: frames 0
    to 3, or 4 to 7 with `bUse`).
  - FishInTank: each fish swims at 5 to 30 (at most 10) to a point
    picked every 0.3 to 1.3 s, or half the time follows the fish before
    it; triggered (the base's `bUse`), they go up to the top to feed.
  - Fire_Gas, a fire's puffs (the fire's `DrawScale` S and `ScaleGlow`
    G; its `ColorFilter.X` the chance of smoke, `.Z` 1 for a normal
    fire, else plasma): stages (`Angle`) of `Phase` seconds, each
    setting the growth, brightening, push and colour change. A white
    flash up (0.2 s, 300 G up), yellow to orange or green (0.3 s), then
    smoke (the holder's `Tex[0]`) greying and sinking away (0.5 s each),
    or the flame shrinking out (0.5 s). A fire's `TweenRate`, if set,
    caps the brightness (a dying fire's).
  - EXP_Trail: fire's colour to smoke's over its life; its texture
    picked again each tick from the explosion's `Tex`, `FadeTex` (in the
    last 2 s, by what's left) or `TranslucentTex` (translucent ones).
    EXP_Smoke (growing by its `Phase`; falling faster than 40, drawn
    back up) and EXP_Column (growing by its `Phase`, at least 0.3) the
    same.
  - EXP_Cinder, EXP_Particle: flickering between the base's colours and
    in brightness, curling about their `Vect2` (the base's `Angle`),
    each leaving a trail of embers; cinders jostled now and then.
  - PlasmaThrower: drawn onto the thrower's axis until 50 short of what
    it hits (`MMEventLocation`), then bounced off it (`WindDir`, the
    normal) and spread over it; green, growing.
  - Lava: falling until under the generator, then still, shrinking away.
  - DisintigrationPhaser: sinking at 30 a second, fading 0.5 a second.
  - ExhaustJet: orange to red to pale, pulled back by the jet's own
    `Velocity.Z`, growing and fading a unit a second.
  - EMPulseRifle, EMPulseRifleQuad: brightening to 1, dimming in the
    last half second; the rings grow by the shot's power (the actor's
    `Acceleration.Z`, at most 1) plus a half.
  - WaterRing: drifting, spreading by its `Angle` and fading by its
    `Phase` a second; gone when dark.
  - SwarmObject, Lava, Weather and the rest: by their velocity and
    acceleration (the swarm's scripts steer each by its acceleration).
- **Arcs** (`CTVE_ChainSpark`), as Engine.dll's natives (read November
  2026):
  - `SpawnChain` makes `NumSegments` copies, each with `SpikeMag`
    (`Use1`) and `EndSpikeMag` (`Use2`) to jitter by, `SparkColor`,
    translucent, the arc's `AxisScale` (its `SetSparkThickness`), then
    lays it out.
  - Laying it out (`Update`, `ForceUpdate`): the base copy gets the
    curve, a cubic from `StartLoc` leaving along `startPath` and arriving
    at `FinishLoc` along `FinishPath`, and the step along it from one copy
    to the next (`Growth` over `NumSegments` - 1). With `bSinVariation` a
    wave across it: a random way (`Alter`, `Amplitude` long, across the
    world's Y and Z, as nothing sets `P1`-`P3`), `WaveLengths` x 6.28
    (`SinScale`). The arc goes to its middle.
  - `ChainSparkTick`: grows (`bGrows`, `GrowthRate` a second, then off at
    1), travels (`bTravels`: `StartLoc`, `FinishLoc` by `StartVelocity`,
    `FinishVelocity`), fades (`bFade`, its `LightBrightness` with it; gone
    below 0) or glows up and down (`bCycleFade`, turning at `MaxGlow` and
    `MinGlow`). Growing or travelling sets `bUpdate`; with it, it's laid
    out again. Its physics (Spark) then places the segments.
  - Before, Reluva laid the segments itself each tick, jolting them only
    on updates, so an arc held still between them.
- **Beams** (`UpdateBeamSections`): the sections start at `BeamOrigin`,
  each `BeamSectionAxesX` further on, turned as the beam, with a wave if
  it has one. Their thickness jitters by `SectionAxisYZRandomFactor`. The
  last section is as long as the head has flown into it (`HeadTime` x
  `HeadVelocity`).
- **Sparks** (`CTVE_Sparks.SparkTick`): when its scripts' `BurstDelay`
  runs out, a burst of `MinSparksPerBurst` to `MaxSparksPerBurst` flies
  out along its X axis at `Speed`. They spread `RangeWidth` and
  `RangeHeight`, fall by the zone's gravity, and are `Size` and last
  `SparkLife` (each give or take its variance), fading as they go. The
  next burst is due in about `AverageBurstTime`, else at random up to
  `MaxWaitTime`. Its light (`saveLightType`) flashes for a tenth of a
  second.
- **Rain and snow** (`CTVE_Weather.WeatherTick`, `InitialSetUp`): drops
  fall up to `SpawnRange` around `SeedLocation`, which its scripts keep
  ahead of the player. There are 4000 x `Intensity` of them (the scripts
  hold it at 0.1); it comes up or down at `DeltaIntensityRate` as it's
  turned on and off. New drops start `DropHeight` above (at every height
  for `InitialSetUp`) and fall at `FallRate` (give or take
  `FallRateVariance`), blown by `DropVelocity`'s wind. The wind drifts at
  random toward up to `MaxLateralMove`. Each drop lasts until it meets
  the world below, traced as it starts, so roofs shelter what's under
  them. None start inside the weather blockers its scripts gathered
  (`Block`: a cylinder or a box), or only inside them with
  `bInvertWeatherBlockers`. Drops left far behind as the player moves
  go.
- **Decals** (`Decal.AttachDecal`, `DetachDecal`): stuck to the world
  surface within the trace distance behind them. They lie flat on it,
  their top along the direction given or their rotation's Z, and are
  drawn over it (modulated) with a depth bias.
- `SetSurfaceTexture`: the surface found by a trace takes another
  texture (the whole world's use of that texture, if asked), saved with
  the game. Its `XORPolyFlags` aren't applied.
- `GetWaterLine(Start, out End)`: `End` is brought back to where the line
  from `Start` crosses the water's surface, found by halving. It is
  `Start` if the line doesn't cross it (bubbles rising out of water).
- **Ghosts** (`MakeGhost`, `GhostActors`): an actor set aside, out of the
  level's list (no ticks, no collision, not drawn) until it's brought
  back. Those placed as ghosts start so (above).
- `ListActors`: an AI's known actors (`ActorList`), each with its entry.
- `ScriptTick`: one actor's tick on its own while the game's paused (the
  HUD's portrait).
- `ResizeActor`: a new collision size, kept only if there's room for it
  (on the floor or under the ceiling, as asked).
- `PickWallAdjust`: a step aside, left or right, that's clear to the
  goal.
- `GetDialogueCameraData`: the current line's camera from the dialogue
  table, in the world or from the speaker.
- `GetRendererFlags` gives 0. `Level.bHighDetailMode` is set from the
  ini's `LowDetailTextures`, so high detail by default: the effects that
  only run in high detail run.

### Saved games

`vm_save.cpp` saves a level being played and starts one from a save.

**The original's saved games** ([save.md](../formats/unreal/save.md))
load as the engine's `LoadMap` loads them: the save is the level itself,
so its package is opened as the map (`?LOAD=<file>.dsa` adds it to the
packages). With `bBegunPlay` set, nothing begins again; instead
(`resumeSavedGame`):
- the ghosts are made too, set aside;
- the level's time is the saved one;
- each actor goes back into its saved state, its state code at the
  statement it was at. One waiting in `Sleep` waits out what was left
  (`LatentFloat`); one waiting in any other latent function makes the
  call again (a move, a `FinishAnim`: its properties have what it needs);
- each actor's body layers and face channels come from its C3D mesh
  state; what it was touching from its `Touching`, so it isn't touched
  again;
- the dialogue's counts come from the saved table, by key;
- the sounds that were playing start again where they'd got to; the
  music becomes the player's `CurrentMusic` again, from where it was;
- the game's unpaused, and the player logs in with `?load`: the game
  type's `Login` takes up the saved pawn (a PlayerPawn without a player),
  and `CTVE_GameInfo` marks the game loaded so `PostLogin` doesn't start
  the level's opening again. The player gets `bJustLoaded`.

Loaded so, `Bajoran_Ship_-_Ke'el_Tul-97.dsa` (M01_SiskoL1A) has Sisko in
his EVA suit on the Ke'el Tul with 91 health and his phaser drawn. The
speech trigger he'd just stepped on was waiting in its state code, so the
scene goes on (Worf: "Sir, Chief O'Brien was injured during the initial
assault..."), and then he walks on. No script accesses None in 20 seconds.
Not restored: the IK targets, bones' adjustments and attachments in the
mesh states (the scripts set them again each tick), the probe masks (what
events each state ignores, which the states themselves give), and the
dialogue's lines under way (none in the example).

**Saving** writes the original's format ([save.md](../formats/unreal/save.md),
`game_save_write.cpp`): QuickSave.dsa, else the level's title and the
seconds played, as the game names its saves, in the game's save folder
(`Saves` in Reluva's folder for it). The original loads them, and Reluva
loads them as it loads the original's (above). Every map saved four
seconds in and loaded back comes back the same: the player where it
was, in its state, with its weapon.

Reluva's own format (its older saves; `reluvaFormat_` writes it) still
loads. A file of it holds:
The game's side is all script:
- F6 (`QuickSave`) and the main menu's Save Game send `SaveGame`.
- The Load Game list asks `DrawLoadGameList`, which hands the HUD each
  game, newest first (`DrawLoadGameFile(Id, name, time)`).
- `LoadByFileID(Id)` gives the HUD its file, and the HUD travels to
  `?LOAD=<file>`. The page loads that game's map (or, for the
  original's, the save itself) and carries on from it.
- F5 (`QuickLoad`) asks to confirm, then travels to
  `?load=QuickSave.dsa`. With none there, the HUD is told (`HudEvent`
  1010) and says so.
- `DeleteByFileID` deletes one.
- `?RESTART` (`RestartLevel`) plays the map again from where it began.

Checked on M04_KiraL1 (quick save, walk off, quick load: the same
place, view and HUD) and with the menus' Save and Load. All 74 maps
save after four seconds of play, load back and play on (`ue-play ...
savegame QuickSave travel "?load=QuickSave.dsa"`).

Not kept: what the engine works out again each tick (posed meshes,
the colliders' list, the limbs' effectors) and the dialogue table's
voice lengths.

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
  through the HUD onto its canvas (the Grigari report: 12 lines and an
  image).

The Maps page shows the level's title and its active objectives while
playing.

### The player's input

`ScriptLevel::controls` (forward, strafe, turn, look up, each −1 to 1)
is what the keys would say. Each tick it sets the input axes the way the
engine does from DefUser.ini's bindings, 20 times their speeds as UE1's
input scales a held key: `aBaseY` ±6000 for `MoveForward` (Speed 300),
`aStrafe` ±6000, `aBaseX` ±3000 for `TurnLeft`/`TurnRight` (150),
`aLookUp`, and the rest 0. Walking caps its acceleration, so only
turning and the scripts' own factors (climbing) feel the scale; at 1x
the keys turned 20 times too slowly and ladders climbed at 6 a second. Then come `PlayerInput` and `PlayerTick`. The
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
(`vm_splines.cpp`), which Engine.dll answers in `ACTVE_Spline::GetData`
and `ComputeRotation`; Reluva does as those do. UnrealEd saves with each
node the `Length` and `Duration` of the segment after it, its `Tangent`
(on The Fallen's, the head's is exactly half the way to the next node,
Catmull-Rom's) and `AveSpeed`, `Length` over the travel time.

- **Timing.** From the node it's asked of, each node's `Duration` is
  counted off in turn along `Next` (round again on a closed chain) until
  the time falls within one, or the chain ends (then at its last node).
  A node first waits its `Pause`; the rest of its `Duration` is travel.
  Travelling, the distance covered after t seconds is `Speed` · t +
  (next `Speed` − `Speed`) · `AveSpeed` · t² / (2 · `Length`), the speed
  changing evenly from this node's to the next's, and `TimeAlpha` is
  that over `Length`, stretched by t / (t + `ExtraDuration` · that); if
  `AveSpeed` is 0 or the node's own `Speed`, it's the time over the
  travel time. Kept to 0-1. (CM10_SWK_L3's Sisko waits 8 seconds at a
  node whose duration is 10.1, its 178 units at 85 the other 2.1.)
  `TimeLine` and `MaxTime`, also saved, aren't used.
- **The path.** The cubic through the two nodes, leaving and arriving
  along their tangents, at `TimeAlpha` itself (not spaced out by
  length), or a straight line with `Interpolation` linear. With `Focus`
  actor follow, the node isn't on its path but at the actor tagged
  `FocusActorTag`, offset by `FocusOffset` turned with it and
  `FocusFollowOffset` (turned too with `bFocusFollowLocal`).
- **Facing**, by the node's `Focus`:
  - Direction (the class's default): the node's own facing (the arrow
    UnrealEd draws on it).
  - Spline: along the curve, towards the point a hundredth further on
    (from one a hundredth back, past halfway).
  - Actor and actor follow: at the actor tagged `FocusActorTag` (kept in
    `FocalActor` while it's still so tagged), offset by `FocusOffset`
    turned with it, and its `PrePivot`; along the curve if there's none.
  - World: at the point `FocusOffset`. Orient: the way `FocusOffset`
    points.
  - Rolled by `RollDegrees`, changing evenly to the next node's.
  - If the next node has `bSmoothRotate` (the default) it turns, by
    `TimeAlpha`, into the next node's facing at its start: the two as
    quaternions, slerped (C3DInterpolateRotationX, the same as
    `Actor.InterpolateRotation`). Not between two spline-focused nodes,
    nor two looking at the same actor.
  - Then the first node's (the one asked) `LocalRotation` is added.
- `TimeAlpha` is also what directors take the field of view between
  their nodes by.

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
after physics. How the natives and the tick behave was checked against
Engine.dll (`execPlayAnim`, `execLoopAnim`, `execTweenAnim`,
`execFinishAnim`, `AActor::UpdateAnimation`, and The Fallen's `C3D`
layer natives); the code is Reluva's own.
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
  `TweenTime` left out is -1: tween only if another sequence was showing
  (over one frame's time), else start at once. A tween sets `TweenRate`
  to 1 / (time × frames) and `AnimFrame` to minus one frame; with none,
  `AnimFrame` starts at 0.001 (0.0001 looping) so the first frame's
  notification isn't missed. A one-frame sequence just tweens. Each
  call keeps `OldAnimRate` and the frame it tweens from
  (`TweenAnimFrame`). While `AnimFrame` is below 0 the mesh is drawn
  blended from that frame to the first. A sequence the mesh doesn't have
  changes nothing.
- A negative rate is scaled by the actor's speed (as the public
  UnrealScript documentation has it): each unit a second of `Velocity`
  plays `-Rate` times the sequence's speed, never slower than
  `LoopAnim`'s `MinRate` times it (`AnimMinRate`). No script of The
  Fallen's passes one.
- `LoopAnim` does the same, looping. If it's looping that sequence
  already, only its rate changes (scripts call it every tick).
- `TweenAnim(Sequence, Time)` goes to its first frame over `Time` and
  stays; with no time it's there at once, not animating.
- `IsAnimating`: a sequence, and `AnimRate` (playing) or `TweenRate`
  (tweening in) not 0.
- Each tick moves `AnimFrame` on, in up to four steps when a step ends
  something (a tween landing, a notification, the end).
  - Passing a notification's time calls the function it names on the
    actor. These are the mesh's own, kept with each sequence (78 of
    The Fallen's meshes have them, 3,190 in all): footsteps, AI firing
    their weapons (`FireWeapon`), the jump's push (`ApplyJumpVelocity`),
    and more. No script sets `bAnimNotify`, so they're called whenever
    a sequence has them, each one passed since the last tick (after
    the old frame, up to and with the new), in order.
  - At the end (1.0), `AnimEnd` is sent. A loop goes on round and sends
    `AnimEnd` when it wraps (not when it began past the end). One
    played once stops at its last frame, finished.
- `FinishAnim` (latent: The Fallen's scripts call it 638 times) clears
  `bAnimLoop` and `bAnimFinished`, then waits for the end: a loop plays
  out its round and stops. It returns at once if nothing's playing. `IsAnimating` says whether one
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
  rest of the skeleton. It fades in over the tween (0.2 s when left
  out), its weight rising from 0 to 1, its time held at the start
  until it's in. The rate is a plain multiplier of the sequence's own
  speed.
- **Holding a pose:** `C3DTweenAnim` fades to the first frame.
- **Replacing one:** a layer started on a group whose layer shows holds
  the old one where it was, under the new one, until the new one has
  faded in (a combo's right punch into the left).
- **Keeping the actor's turn:** `ClearYaw`, `ClearPitch` and `ClearRoll`
  keep the part's first bone turned as it is under the layer about the
  actor's up, side and forward axes (the layer's turn less its twist
  about each); the bones below take the layer as it is. `CTV_Human`'s
  melee sets all three except when swimming, so a punch while running
  keeps the spine turned with the run and the arm punches.
- **Speed:** `C3DLoopAnim`'s `MinRate` is ignored, as the engine does.
- **Ticking:** a layer moves on at its rate and calls the sequence's
  notifications. One played once sends `C3DAnimEnd(Group)` at its end
  and holds its last frame; a loop sends none as it goes round, nor
  does a tween. The scripts answer `C3DAnimEnd` with
  `C3DTweenOutAnim(Group, 0.1)`, which fades it out, and then it goes.
- **Waiting and asking:** `C3DFinishAnim(Group)` (latent) stops a loop
  looping and waits for that end, and `C3DIsAnimating(Group)` says
  whether one's playing.

Faces are channels, as Engine.dll keeps them (after the mesh's own
animation groups, in the actor's C3D mesh state):
- `C3DPlaySkinAnim()` adds one and names it `sk` and its place among the
  actor's groups (the mesh's FullBody, LowerTorso and UpperTorso first:
  Pawn's `Phonemes`, `Blink`, `EyeBrows` and `Mouth` channels are `sk3`
  to `sk6`), counting up while another channel has the name.
- `C3DSetSkinAnim(Tag, Sequence, FrameTime, Weight)` sets a channel to a
  time in a face sequence (seconds: the scripts pass frame × 0.033) at a
  weight, at once.
- `C3DTweenSkinAnim(..., TweenTime)` cross-fades from what the channel was
  set to last (not what shows mid-tween; the old time read in the new
  sequence) to the new, straight over `TweenTime`: the two shapes fade,
  the time doesn't sweep through those between. `C3DTweenSkinAnimDelay`
  is the same from the channel's next tick (the last asked wins): the
  lip-sync's `C3DSpeechEvent` uses it, as it comes from the audio driver.
- A channel's time never moves on its own. One held at time 0 shows
  nothing.
- `C3DClearSkinAnim` removes one. `C3DCopySkinAnimState(Other)` gives each
  of this actor's channels the state of the other's of the same tag;
  none made or removed.
- The face sequences: Blink 2 keys (0, 1/30 s), EyeBrows 3, Phonemes 8 (k/30
  s, a mouth shape each), Mouth (four PAWNAI meshes; no script uses it).
- **Drawn** as the engine does it, after the body is posed: each channel's
  entry in the mesh's face table (the clip of its name, by index; see
  [mesh.md](../formats/unreal/mesh.md)) at its time, between keys
  blended, placed in its attachment point's frame (the `FaceAttach`
  triangle on the posed mesh, so it follows the head), and each vertex it
  lists pulled there by the channel's weight from where the body put it.
  At the scripts' weight of 1 those vertices are the keyed shape. A tween
  cross-fades that from the channel's last time and weight. Channels go
  in turn, each from the last's result.
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
  It works as Engine.dll's does. The scripts compare the result against
  0, 0.1, 0.3, 0.5 and 0.99, and the heads multiply a weight by it.
  - **The target's middle** is its Location raised by `LocationZAdjust`
    (the heads pass the head's height). H is its `CollisionHeight` +
    `HeightAdjust`.
  - **It returns 0** unless the middle is closer than `Radius` and the
    cosine of its angle off `Rot`'s forward axis is over `CosFOV`.
  - **A point is seen** if a line to it (through the world, movers and
    actors, but not the looker) meets `Target` first. So another pawn in
    the way hides it too.
  - **Without `bCalcVisible`** (or with H 10 or less and no
    `bCheckExtents`), the middle is tried first: seen gives 1. If it
    isn't, and H > 10 or `bCheckExtents`, the grid below is tried, and 1
    comes at the first point seen.
  - **The grid:**
    - two rows at ±0.9 H if H > 10, else one at the middle's height;
    - with `bCheckExtents`, two points a row, 0.9 × `CollisionRadius`
      either side, across `Rot`'s forward axis (level, and shorter the
      steeper the look);
    - the lower row is a step further over, as the original's loop leaves
      it.
    
    With `bCalcVisible` it returns the share of the grid's points seen
    (quarters or halves; the middle isn't one of them).
  - **`BestHitLocation`** is the first point seen. The turrets aim there.
    If none is seen it's left as it was.
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

Drawing a face: its channels are the vertex-key clips (`Blink`,
`EyeBrows`, `Phonemes`, [mesh.md](../formats/unreal/mesh.md)). Each
clip's first key is the same neutral face, in `BaseReference`'s pose (on
Quark to within a few units, at the hands); the later keys move 8 to 32
of his 377 vertices. A channel is its clip's key at its time (between
two, blended) less the neutral face, times its weight. Each vertex's
share is turned by its bones from `BaseReference`'s pose to the one
drawn and added to the posed vertices. A channel at the clip's start
adds nothing, so only the faces away from neutral are posed at draw
time. On DS9_Ops the scripts set Phonemes 0.07 to 0.1 s in, EyeBrows
0.07 and Blink 0.01: Dax's lids drop, her brows lift and her mouth
changes shape.

Not done:
- **Lip sync:** the game has no lip-sync data, so Phonemes is only ever
  where the head's expression script puts it.

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
  `C3DGetJointLocalPos` gives it in the skeleton's own space, with only
  its axes swapped to Unreal's: no Location, PrePivot, Rotation,
  DrawScale or mesh Scale, in the skeleton's units (Engine.dll's
  `execC3DGetJointLocalPos`). A bone that isn't there, or an actor with
  no skeleton, leaves the out values as they were (the original logs
  "Bone doesn't exist").
  - **The axes are in the scripts' order:** X is the skeleton's -Z, Y its
    -X, and Z its Y. This was worked out from what the scripts do with
    them, and fits Kira's skeleton in every case:
    - a head's -Z is where it looks (`Pawn`'s look-at, `CTVE_Pawn`'s
      sight);
    - the eyes, nose and mouth are along -Z and up -X (`NosePos`,
      `CTV_ColdBreath`);
    - a foot standing flat has Z along the ground's normal and X
      toward its toes (`CalcFootZ`).
  The `Rel` forms:
  - `C3DGetJointLocalPosRel` (Engine.dll's) gives the same place as
    `C3DGetJointLocalPos`. Its rotation is the bone's turn from its bind
    pose (`Bind^-1 * Current`, in the skeleton's space), in Unreal's
    axes. "Rel" is relative to the bind pose, not the parent.
  - `C3DGetJointWorldPosRel` (Engine.dll's) gives the same place as
    `C3DGetJointWorldPos`. Its rotation is the bone's turn from its bind
    pose (`Bind^-1 * Current`), carried into the world as `WorldPos`
    carries the bone's own. It's relative to the bind pose too, not the
    parent. A bone that isn't there leaves the out values as they were.
  - No script calls any of the three.
- `C3DDoesBoneExist`.
- `C3DGetAttachPtCoords(Name, out Pos, out Orientation)`: an attachment
  point in the world (`GunTip`, `FirePoint`, `KickPoint`...), as
  Engine.dll's `PointMatrix` places it:
  - It's worked out in the skeleton's space, then placed as the mesh is.
  - On a triangle (type 1): the point's turn and offset in the
    triangle's frame as posed. The frame runs from corner 0, with X
    toward corner 2, Y the normal and Z = X x Y. No bone is involved.
  - On the mesh (type 2): its turn and offset from the skeleton's
    origin, as stored.
  - Of another type, or of kind 7 (the stored matrix's identity flag):
    no turn and no offset.
  - The orientation is in the scripts' order of axes, as the bones'
    (X the point's -Z, Y its -X, Z its Y).
- `C3DGetAttachPtCoordsPrecomputed`: the places worked out in advance
  (`FireGunTipLoc`), the same way as a type 2 point.
- `C3DGetAttachPtOffset`: the point from the mesh's origin, with no
  DrawScale. Its rotation is the turn as the skeleton has it, without
  the swap of axes (as Engine.dll's). Also `C3DGetNumAttachPts`.
- A part the mesh hasn't answers with the actor's own location and
  rotation, so what starts there starts at the actor.
- **Attaching:** `C3DAttach(Obj, ObjPt, ThisPt)` keeps this actor, from
  each tick on, with its point `ThisPt` on `Obj`'s `ObjPt`. It's turned
  so its point's axes are `Obj`'s point's, and moved so the two meet.
  This runs after every actor's tick, until `C3DDetach`. As Engine.dll's:
  - A point not named is the mesh's first. A named point that isn't
    there, or a mesh with no points, ends the attachment.
  - The child's own point is undone in the parent's DrawScale. The
    child's own DrawScale and PrePivot aren't allowed for.
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
animations. As in Engine.dll, each turn is applied to the pose at once,
on top of the ones before it, and the next tick's pose starts again
from the animations, so the scripts make them anew every tick (they
do: `UpdatePlayerIK`, `UpdateHeadAnimation`). Reluva keeps a tick's turns
in the order made and applies them in that order to the pose for
drawing, for the bone natives and for the attachment points; they
last until the scripts' next tick replaces them:
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
- **`C3DRemoveRotation(Bone, bYaw, bPitch, bRoll)`:** takes the bone's
  turn away from its bind pose (the skeleton as modelled) apart into
  yaw, pitch and roll (roll first, then pitch, then yaw, in the
  skeleton's axes), drops those asked for, and puts the rest back: the
  bone where it is, turned from its bind pose by what's left. As in
  Engine.dll, there's no yaw when the bone's X row stands within 0.0001
  of straight up. There's no roll when the cosine between the yawed X
  and the turned X is 0.9999 or more (under about 0.8 degrees).
- **Read from Engine.dll:** `C3DApplyRotation` (degrees, about the
  bone's own axes, before its turn: pitch X, yaw Y, roll Z), and
  `C3DApplyWorldRotation` (the rotator in the mesh's unturned frame,
  through the fixed swap of axes, the actor's own Rotation left out).
  Both match Reluva's. Their turns last until the pose is next worked
  out from the animation, once a tick.
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
  the shorter way round, turning evenly: Engine.dll makes the two
  quaternions and slerps them (not each angle on its own). The scripts ease
  the IK turns and the head's look with it. While it was missing, every
  foot target came out as turn 0, which faced the feet backward.

On M01_KiraL2, standing, Kira's points and bones are:
- head bone 35 above her middle (her top is 47);
- feet 40 below;
- hands at her hips, 14 to 15 units to each side, the right on her
  right;
- `Face` in front of her head.

On M11_KiraL1, an ensign's tricorder hangs upright at her belt. On
M04_KiraL1, once 2 is pressed, Kira holds the phaser rifle at her hip
facing where she faces, both hands on its grips, its muzzle 27 units
ahead of the right hand's.

Not done:
- The meanings above are Reluva's readings of how the scripts use the
  natives: the order of a bone's axes, the axes `C3DApplyRotation` turns
  about, the actor's axes for `C3DApplyWorldRotation`, and what reaches
  in an IK chain.
- **The IK, as Engine.dll's** (`C3DApplyIK` and its solver, read November
  2026):
  - **The chains are data.** Shared.u's `Male` keeps eight (the player
    takes them through its alt meshes; only `CTV_Player` sets IK
    targets): RightArm and LeftArm (clavicle to hand), RightLeg and
    LeftLeg (thigh to foot), and a hand's or foot's own. Each names its
    root and end bones, a 4x3 effector matrix in the end bone's axes
    (identity but for RightHand, about 7 degrees), steps of a bone and
    an axis (the arms': forearm Y, upper arm Z, upper arm X, forearm Y,
    clavicle X), 4 iterations and a tolerance of 0.
  - **The bones' joint limits** are the 10 floats after each bone's
    children: two of +-1e38, two more, then min and max about Y, X and Z
    in radians (Male's right forearm 4 to 147 degrees about Y, about 1
    about X; a default of +-30). Read into each bone's record.
  - **The solve:** each step turns its bone about its one axis. The
    bone's turn from its bind pose (`M0 = local x Bind^-1`) is taken as
    Euler angles for `E = Rz Rx Ry`, and that axis's angle is set afresh
    to bring the end bone's origin (the effector: no attachment point)
    round toward the target. It's kept within the bone's limit for that
    axis, and X also within +-90 degrees. Then each of the chain's bones
    is slerped from where it was by the chain's weight. On Sisko and
    Kira holding the rifle, the hands come within 0.02 to 0.3 units of
    their targets.
  - **The hand's or foot's chain** sets that bone's turn outright (the
    wanted axes with the chain's effector turn undone), slerped by its
    weight.
  - **`C3DApplyIKOnBasePose(IkChain, Val)`**: with `Val` not 0, the
    chain's bones go back to their bind pose before the solve. It isn't
    "before the layers": the solve is always after them.
  - A skeleton with no chains of its own or borrowed keeps Reluva's
    earlier two-bone solve. No other script sets IK.
  - **A turn from another actor** (`C3DSetIkTargetOrientation(IkChain,
    Target, RelRotation, ..)`): the native only stores the request (the
    target by name, the rotator, the weight); each frame the engine
    makes the target's `Rotation` into the character's space and
    multiplies the axes of `RelRotation` (`GMath.UnitCoords /
    RelRotation`, rows copied as they are) on before it, row-vector
    style. So `RelRotation` is turned within the target's own axes, as
    Reluva does (`inFrame`): no other axes or order (read at
    0x1032ac30).
  - **The feet** (`CTV_Player.ApplyFeetAlignment`): standing still
    across a step, the player is lowered to the lower foot
    (`FeetPivotZ`, `LocationOffset.Z`) and the higher leg's chain lifts
    its foot back onto the step. Seen on M01_SiskoL1A across the
    20-unit step at (960, 380): lowered by 19.9, the left knee bent with
    its foot on the step, the right leg straight down to the floor.
- Points from the origin are in the skeleton's units
  ([mesh.md](../formats/unreal/mesh.md), `reluva-tool ue-points`). With them, Kira holds the phaser rifle at
  her hip, the right hand on its grip at the back and the left under the
  barrel. That needed `Rotator * Rotator` (The Fallen's own operator,
  `Object.uc`'s native 292) to turn the second rotator on in the
  first's frame, as `CTV_Player` turns the player's weapon by its
  `IK_PreRotate`: it was multiplying them part by part, so a weapon with
  no pre-turn was left at no rotation at all, facing the world's X
  whichever way she faced.

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
  C crouches, as the game binds it to `Duck` (`bDuck` held and `aUp`
  down). Ctrl or F fires (`bFire` and `Fire`). The number keys call the exec
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
    animation, it's drawn blended from the frame the tween began at to
    the animation's first.
  - Sprites.
  - Movers where their scripts have moved them.

  Lighting is the level's lightmaps, as worked out when it loaded.

While playing, V lets the camera loose: the keys fly it round the level
as it plays on, the player standing, and V again gives it back to the
game.

`reluva-tool ue-play` plays the page without a window, through a
software renderer (`tools/soft_renderer.cpp`: textured, depth tested,
blended triangles, enough to see a frame), driven by steps:
`play`, `wait <s>`, `hold <key> <s>`, `down`/`up <key>`, `press <key>`
(letters, `left` `right` `up` `down`, `space`, `enter`, `escape`, `f1`...),
`travel <url>` (as the game's `ClientTravel`: "CM_SpaceStation/intro",
"?load=QuickSave.dsa"), `saves <dir>` (where games are saved),
`savegame <name>` (saved now: `QuickSave`, else the next `Save<n>`),
`wav <file>` (the sound heard since the last, mixed offline), `sounds`,
`watch <actor>` (the camera let loose in front of a live actor),
`teleport <actor> <dx> <dy> <dz> <yaw>` (the player put by an actor,
facing yaw degrees, falling), `where` (the player's place, velocity,
state, physics and animation), `actor <name>` (a live actor's place,
state, collision and look), `lines` (the dialogue lines said so far),
`view` (the game camera's place, turn and field of view, and whether
it's in solid), `prop <actor> <property>` (a live property as text, a
struct's fields listed), `console <command>` (as the player types it: `set
DS9_FederationPhaserRifleAmmo AmmoAmount 20`),
`drag <dx> <dy>`, `click <x> <y>`, `shot <file.ppm>`. Checked that way:
- **M01_KiraL2:** Kira runs down the stone corridor from behind, the
  camera swinging to her side, and turns into the wall with the camera
  pulled in beside her.
- **M04_KiraL1:** she rides the lift down from her start and stands in
  the turbolift.
- **M01_KiraL1A:** the opening's directors fly over the monastery
  bridge, then follow her and a monk, letterboxed.

**Speed** (`ue-play`, 20 s of play, 600 frames; November 2026). `ue-play`
prepares each frame for drawing as the Maps page does: it poses, lights
and uploads the characters and runs the HUD. Only the drawing itself is
left out.
- M10_WorfL1A, the slowest map, now takes about 12.5 to 15 s; it took
  21.5 s before the work below. M03_KiraL2 takes about 5 s (12 s before).
- The scripts alone (`ue-run -all`, no drawing at all) take 128 s for
  all 74 maps. The slowest is M11_WorfL1, at 2.9 s for its 20 s.
- The work:
  - **Fire, water and the other procedural textures** tick only when
    they're drawn, as UE1's do. Engine.dll's `UTexture::Update(Time)`
    ticks a texture by the time since it last updated, and the renderer
    gets each texture it draws through `UTexture::Get`, which calls it.
    The Maps page now notes which textures each frame draws
    (`TextureUseRecorder`) and ticks only those. Before, every one in the
    level stepped every frame, seen or not.
  - **The water texture's step and slopes** read their rows directly,
    without a modulo for every cell. The picture is the same.
  - **Packages are read whole at once.** They had been read a byte at a
    time through stream iterators, a fifth of a level's start.
  - **Answers kept:**
    - `VmObject::find` with a string literal remembers its slot for each
      class.
    - The level's walls between a light and an actor (`wallBetween`) are
      remembered for the same two places, since the BSP doesn't move. A
      character standing still asked it of every light each frame.
  - **The overlay:** only the rows drawn on the frame before are
    cleared, not all 2.7 MB of it.
- What's left most, from callgrind on M10_WorfL1A:
  - Posing, lighting and uploading the characters for drawing: about
    half, the IK's reaches (worked out twice a tick for each character
    holding a gun) among it.
  - The HUD's scripts: a tenth.
  - The viewer's own text box: a tenth.
  - Sounds decoded from MP3 the first time each is heard, and meshes
    read as they first come into view: once each.

The biggest maps' ticks took twice as long. Profiled on M03_KiraL2
(callgrind, then stack samples later in the run), these made it about
twice as fast (20 seconds of play: 21.6 s, now about 10.7 s):
- The moves' loops (blocking, touching, traces) go through only the
  actors that collide with actors: a list made each tick (again when
  `SetCollision` changes one), not all 4,000. Traces read the classes'
  collision slots rather than asking `isA` by name.
- Movers far from a move are passed over by their brush's bounding
  sphere before the costly test in their own space. That also ended a
  bug: a flat mover brush (a force field's sheet) makes a BSP of one
  plane, solid all the way behind it; CM04_Lab refused 301 placings 2000
  units from one. Overlaps are also kept inside the brush's box.
- Names are lower-cased on the stack; a struct's properties are found by
  an index; a native's code, whether a function is `&&` or `||` and
  whether it has out parameters are found once; frames make their
  temporaries' list only when needed; vectors are written in place;
  values copy their string and fields only when they have them.

The effects' and Core's random numbers start again with each level, so
a map runs the same alone or in `ue-run -all`.
