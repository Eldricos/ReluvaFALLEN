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
  before the last of it, the slowest (M10_WorfL1A) 11.5. The last of
  it:
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
| Walking | Acceleration (flattened, at `AccelRate`) against ground friction (the zone's, 8) up to `GroundSpeed`. A smooth move, then down onto the floor if one is within a step, else falling (`Falling`). A floor is a surface facing up at least 0.7 (about 45 degrees). Against a wall or a mover, `HitWall` (the AI opens doors from it); The Fallen's player gets it as its walking state reads it: X how head-on (its way of moving against the normal), Y its speed, so it stops running against a wall. A move that gets nowhere (the crease where a ramp meets a slope too steep) tries a step too, and is told of the steep wall. |
| Falling | Gravity (the zone's, −950) and `AirControl` of the acceleration, up to the zone's terminal velocity. Off a wall or a slope too steep to stand on, `HitWall` and the rest of the fall slides along it. On a floor, `Landed`, then walking (pawns) or none, keeping the speed across. As a fall starts, the actor's `FallHeight` is set where it is: The Fallen's `TakeFallingDamage` kills at a drop of more than 800 from it, and its scripts set it only while rising in a jump. Left at 0, every landing on a map far below the origin was fatal. |
| Swimming, flying | Acceleration against fluid friction up to `WaterSpeed` or `AirSpeed`, smooth moves. Swimmers sink or float by `Buoyancy` against `Mass` and fall once out of water. |
| Projectile | Its velocity plus acceleration; `HitWall` on the world. |
| Rotating | `RotationRate` each second. Any physics also turns toward `DesiredRotation` with `bRotateToDesired` (not a player's pawn: its input turns it). |
| Moving brush | While its script has it going (`InterpolateTo` sets `bInterpolating`): a mover from its old key toward its next by `PhysAlpha`, eased in and out if it glides by time. It carries the actors based on it and sends `InterpolateEnd` at the end. |

The Fallen added its own modes (past 10): the player's climbing,
free flight, fish, interpolation and one for each kind of effect.
`vm_climbing.cpp` and `vm_effect_physics.cpp` work them; the original
code isn't known, so each is Reluva's reading of what the scripts set
and leave undone.

| Physics | What it does |
|---|---|
| Interpolating, CTVE_Interpolating | Along a path of interpolation points (`SpecialEvent`'s player paths, the intro's camera): from `Target` to its `Next` by `PhysAlpha`, at `PhysRate` times the point's `RateModifier`, placed and turned between them. At each point, `InterpolateEnd` on the point and the actor (their scripts end the path), then on to the next (past one with `bSkipNextPath`). No shipped map has interpolation points, so it isn't seen. |
| Trailer | At its owner's place (and turned as it, with `bTrailerSameRotation`), as UE1's. |
| Climbing | Up and down a ladder at its `Acceleration.Z` (the scripts': `aForward` x 0.02, 120 a second). At the top, with no ladder at the chest, the ledge found there and `ChangePhysics(Clambering)`; down onto a floor, `Landed` and walking. |
| Hanging | Along the ledge at its acceleration (`aStrafe` x 0.008), while there's ledge under its hands. |
| Clambering | Up onto the ledge as its animation (`ledgeu`, `BigStep`) plays: the rise over its first two thirds, then forward past the wall. At the end, falling the last bit, so its scripts' `Landed` walks again (their `AnimEnd` would only start the animation again). |
| RotatingYaw, RotatingPitch | Turning about the one axis by `RotationRate`. |
| Particle | No gravity: its velocity changed by its acceleration (a ship lifting off, debris settled), moved against the world, `Landed` or `HitWall`. One that makes copies stays where its scripts put it. |
| Newtonian | Free flight by momentum (the Vulture, the Wraith, the Arachnid): its acceleration changes its velocity, up to `AirSpeed`; no gravity, no friction. |
| Fish | Toward its goal at `AccelRate` up to `WaterSpeed`, against the water's friction, facing the way it swims; it can leave the water (its scripts send it back). |
| EXP_Trail | An explosion's trail: by its velocity and acceleration, through everything. |
| The rest | Only their copies move (see Effects). |

**Finding a ledge or a ladder** (`climbCheck`, after each walking or
falling move of a `CTVE_PlayerPawn` pushing forward): a wall straight
ahead, just above a step's height or at the chest. A ladder if its
texture's `TexTag` says so (`TT_Ladder`): `ChangePhysics(Climbing)`.
Else its top, traced down onto from above just past its face, as long
as nothing's in the way up there. Its height goes in `LedgeHeight`, how
deep it is in `LedgeDepth`, the room over it (as a half-height, which
the scripts compare with theirs) in `HeadRoom`, the wall's normal in
`LedgeNormal`. The scripts' `ChangePhysics` does the rest:
- A ledge up to 100 above the feet, deep enough and with room, pushed
  into for 0.15 seconds (at once in a jump): `Clambering`.
- A higher one while falling, within the hands' reach (`ArmCollisionHeight`
  over the head, up to 30 more): the player is put hanging from it,
  facing the wall, and asked to hang (`Hanging`). Forward from there
  climbs up (the scripts' `PlayerHanging`); jump or back lets go.

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
  - the level's `Talker`, `Listener`, `CurrentLineType`,
    `CurrentLineIndex` and `CurrentLineFlags` set for the scripts (the heads look at the
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
    of those that fit, the best as above), else the one it runs on to,
    else a return line or what's left of a hail (below), or the end: the
    talker and listener cleared.
- **Which lines, asked for by type** (not by tag): this map's mission's,
  general ones, and another mission's only when its conditions tie it
  to the story (`DS9Phase`...). Without that, a hail on M06 got mission
  10's "Aye, sir.", and M04's guards M10's lines. Asked for by a tag
  that no line of that type has, the line with the tag still does: the
  training room asks for `TRAIN01` as TRN, and the table makes it EVT.
- **Hails** (`PlayDialog("HAL", None, <crewmate>)`, the HUD's
  communicator): first a need, if the player has one: an option (HOP) to
  that crewmate whose conditions hold ("Chief, I need rifle ammunition",
  `LowAmmo`; the line being blocked). Else a line of this mission's, as
  above. Else the player's other options: asking for a report. Then the
  answer: its NEXTLINE (O'Brien's "Stand by" then "Energizing… now",
  which beams the ammunition down, in a beam-down zone; elsewhere the
  mission's refusal; below), else that crewmate's report (HBO, to the player or
  to no one). Then the sign-off: its RETURNLINE, else the player's HCL
  (not after the crewmate has signed off). On M06_WorfL1A: "Captain,
  what is your status?", Sisko's mission report, "Understood. Worf out."
  On M03_KiraL2 with 20 shots left: "Chief, I need rifle ammunition.",
  "Sir, I can't beam anything to your current position - there's too
  much ionic distortion.", "Acknowledged. Kira out."
- **The supply beam-down**, played through on M03_SiskoL1B (`ue-play
  ... press 2 console "set DS9_FederationPhaserRifleAmmo AmmoAmount 20"
  teleport DS9_BeamZone1 0 0 0 0 press f1 press f ...`): with 20 of
  the rifle's 150 shots left, standing in the map's `DS9_BeamZone`, the
  hail goes "Chief, I'm running low on ammunition." (`LowAmmo( 0.2 )`,
  which also sets the rifle's `LastCheckForMinimum` to 30), "Stand by,
  Captain." (`InBeamDownZone`: the HUD's `InBeamZone`, inside a zone's
  radius by its signal strength), "Energizing… now." (EVENT
  `-BeamDownAmmo -BeamDownShield`), "Acknowledged. Sisko out." The
  ammunition clip is beamed in where Sisko faces, the transporter's
  strips and violet sparks round it for a second or two; walking onto it
  he stoops to pick it up and has 40 shots.
  - **A line's EVENT** is words apart by spaces: each is a tag to
    trigger (the dialogue input's `DoEvent`), or with a `-` before it one
    of the HUD's beam-downs (`DoSpecialEvent("BeamDownAmmo")`: the
    script compares the whole word). Passed whole, nothing was beamed.
  - **Which NEXTLINE answer**: of the lines the key matches whose
    conditions hold and that are for whoever the player plays (`KEN`'s
    "Stand by, Major" only for Kira), this mission's first, then another
    mission's its conditions tie to the story, then the general ones,
    then another mission's that only happen to fit. Before, M04's "I
    can't get a signal lock" (`G04`, no condition) beat the general
    "Stand by, Captain".
- **A condition's arguments**: a number where a name parameter comes
  first leaves the name out: `LowAmmo( 0.2 )` is any weapon under a
  fifth (taking 0.2 for the weapon's name, it was never true).
- **RETURNLINE**: a line's return line is played once what its NEXTLINE
  leads to is done ("…What's next?", O'Brien's answers, then Kira's
  "I'm on my way. Kira out.").
- **Running on**: a line with no NEXTLINE goes on to the one numbered
  just after it, of the same table, mission, type and speaker, with no
  tag of its own (or the same). Kira's log on the Promenade asks for
  `START_K11A3` (010), which runs on to "I haven't been able to sleep…"
  (011); M01's opening, 005 to 006 and 007.
  A cut scene's (CIN) lines are a conversation, so they run on across
  speakers: to the next line numbered in the table (past gaps, 009 to
  010) up to the next one with a tag that starts a part of the scene. A
  tag naming the line's own speaker (Zoto's lines tagged `ZTO`) doesn't.
  Checked against a recording of the original: the opening's lab
  (Title) said only Terell's first line of each part and ran on 20
  seconds early; now its thirteen lines, from "Increase the magneton
  pulse" to the scream, in step with the original's to within a second.
- **The CAMERA column's flags**: `V:1` a voice-over (a log), `H:1` heard
  over the communicator. They become `Level.CurrentLineFlags` 16 and 32
  (Pawn's head tracking then doesn't turn to the talker), and an H line's
  speaker is the HUD's picture of them (`GetSpeakerActor`'s
  `bForceUseHudMeshActor`). A line's place and turn (`A:`, `R:`) are the
  scripts' to ask for (`GetDialogueCameraData`, the cut scenes'
  directors and speech triggers).
- **Who says it**: the actor whose `Dialogue_Tag` is the line's tag if
  there's one (the Bajorans who hand over their cards, `BM2`), else the
  nearest with the speaker's code.
- **Notes for the voice actors**, text all in brackets ("[GROAN IN PAIN
  #1]", the Grigari's "[Blah, blah, blah]"), are heard but not shown.
- **The AI's own lines**, which no script asks for (`tickBarks`). A pawn
  with a speaker code, alive and within 1500 of the player, says one:
  - FSE when it first has an enemy ("We have an armed militia operative
    loose in the arena");
  - LSE when it loses it, KIL when it's dead;
  - SUR when its scripts put it in `Cowering` or `Surrender`;
  - AMB now and then while it has no enemy (every 20 to 60 seconds), else
    REP (the injured: "Hey! Over here!").
  
  Only its own lines: its speaker code, and a tagged line only for the
  actor with that `Dialogue_Tag` (a Vorta's watch-station report is that
  Vorta's). The start of what it says, not the lines that run on from it.
  Their conditions are about it: an actor parameter (`FriendClose( 500 )`,
  `PlayerFaceEnemy`, `EnemyIsA( DS9_JemHadar )`) is the speaking pawn,
  not written in the table. One at a time, not over other dialogue,
  4 seconds apart for each pawn. When the game said them is Reluva's
  reading: the tables say only what. `DialogTick` itself does nothing.
- Over all maps' first 20 seconds, 36 dialogues start (123 lines said),
  the AI says 62 lines, and 2 calls find no line: CM11_LabRitual's
  `START_G11B` (mission 11's table isn't in the game: its voices are, in
  `m11_Dialogue.uax`, as are `m12_` and `trn_`) and a speech trigger on
  M04_SiskoL1 with no type.

Each line's voice plays as it starts (below, Sound). Lip-sync: the game
ships no lip data for its voices, so the mouth's shapes come from the
line's letters (above).

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
  camera (its own field of view if given, else the world's). It lasts
  until the next one or the end of the frame. A frame of the whole screen
  is the world's view: `ResetCanvas` asks for one with the HUD actor's
  own place and rotation, which nothing sets, so taking it as the
  player's view is Reluva's reading.
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
(`level_audio.cpp`, through `common/audio`'s mixer). Where it's heard
from, how it fades and what's what is Reluva's reading of how the
scripts use it; the engine's own code isn't known.

- **`PlaySound(Sound, Slot, Volume, bNoOverride, Radius, Pitch,
  bNoHearNoise)`** and `PlayOwnedSound`:
  - Heard from the actor, following it while it plays.
  - Loudest where it is, fading to nothing at `Radius`, on the side the
    camera hears it.
  - Left out: `Volume` and `Pitch` are the actor's `TransientSound` ones,
    `Radius` its `TransientSoundRadius`, else 25 × (`SoundRadius` + 1).
  - A slot (all but none) holds one sound an actor. A new one stops the
    old, unless `bNoOverride`, when it isn't played while the old one
    is.
  - Unless `bNoHearNoise`, it's a noise the AI hears, as loud as its
    volume (below). The Fallen's weapons pass it for all their sounds
    but the empty click; footsteps don't.
- **Ambient sounds:** each actor's `AmbientSound`, looping while it has
  one. Heard as far as 25 × (`SoundRadius` + 1) units, at
  `SoundVolume` / 255, its pitch `SoundPitch` / 64. It starts once the
  camera is in reach.
- **Through walls:** a sound in hearing (one played, or ambient) with the
  level's walls between it and the camera is heard half as loud and
  muffled: the mixer's low-pass (`Mixer::setMuffle`, its cutoff falling
  from the full band to about a kilohertz). Only the BSP counts (a
  door's own sounds aren't muffled by the door), and not the last 24
  units before the sound nor a sound placed inside solid (ambient sounds
  set into a wall or ceiling sound from its face). It eases in and out
  over a quarter of a second. Voices and music aren't placed, so never
  muffled. `ue-play`'s `sounds` marks those muffled.
- **Music** is The Fallen's streamed sounds, the MP3s in `mp3/`
  ([sound.md](../formats/unreal/sound.md)):
  - The player's `CurrentMusic` names the track. The start sets it
    (`OpeningSoundTrack`).
  - A music trigger (`CTVE_MusicEvent`) plays a track through the HUD's
    `PlaySound`. Playing a streamed sound makes it the music, at that
    volume, started where the HUD's `StartTimeOffset` says. Each play
    starts it again.
  - Setting `CurrentMusic` to None fades it out over a second.
  - When a track ends, `CurrentMusic` is None again. The filler music
    (`CTVE_FillerMusic`) waits for that, then plays its own as an
    ambient sound that it moves towards or away from the player to fade.
  - A loaded game starts its music from the beginning.
- **Voices:** a dialogue line's Sound (its record, in the package of its
  table's name) plays as the line starts. It's heard everywhere, over
  the communicator too, one at a time (a new line's stops the last).
- **`GetSoundDuration(Sound)`** is its length; `CTVE_Dialog` times its
  lines by it.
- **Footsteps** go by the floor's material: `GetSurfaceTexture(Start,
  End)` is the texture of the level's surface a line meets (`TexMaterial`:
  cement, carpet, metal, water...). The surface is the node whose plane
  the trace stopped at, or the coplanar one whose polygon holds the
  point.
- **The volumes** are the game's settings (`[Engine.Engine]
  AudioDevice`'s section: Miles, in Default.ini `MasterVolume=160`,
  `SFXVolume=190`, `MusicVolume=255`, `SpeechVolume=255`, each out of
  255). Each kind's is under the master's, and the options page changes
  them as it goes. So do `UseStereo` and `ReverseStereo`.
  `SetMasterVolume` sets the master for now (a level fading out), and
  `KillSound` stops every sound.

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
- **Copies by physics mode** (`moveMultiMeshes`):
  - Placed by their own natives or scripts, only aged: arcs (`Spark`),
    beams (`BeamSustain`), the M4 rifle's gas cloud (`GasCloudM4`,
    fading).
  - Particle, Smoke, SteamJet: with a generator (`CTVE_Generator`, the
    holder's owner), growing by its `DeltaSize` a second, fading with
    `bFadeGlow`, shrinking away with `bFadeSize` if they don't grow,
    blown by its `WindForce` when `bIsWindy`. A steam puff stops when
    it has slowed to nothing and hangs there (`bUse`).
  - Bubble: rising to the water's surface (the generator's
    `SeedLocation`), where it pops: the generator's `bMMEvent` and
    `MMEventLocation` (its scripts spawn a ripple).
  - WaterRing: spreading by its `Angle` a second, fading.
  - FanParticle: along the fan's axis from a point on its disc (`Phase`
    -1 to 1 of its reach, the side the fan's `Acceleration.X` says, at
    its `Acceleration.Z`), fading toward the end.
  - SpiralJet: out along the steam's X axis on a widening, turning
    circle; growing and fading by its base's `Use1` and `Use2`.
  - ExhaustJet, PlasmaThrower, EXP_Smoke, EXP_Column, HotSpark,
    CTVE_Sparks, DisintigrationPhaser: flying, growing and fading each
    by its own fields (the exhaust's base `Use1`, the smoke's `Phase`,
    the column's `Angle`...).
  - EXP_Particle, EXP_Cinder: falling by the explosion's
    `ParticleForces` (rising by the cinders' `wind`, jostled by their
    chaos), fading; some flicker to the base's colour.
  - EXP_Trail: fire's colour to smoke's (the base's `Vect1` to `Vect2`).
  - EMPulseRifle: still, growing by the shot's power (its
    `Acceleration.Z`, not an acceleration) and fading; EMPulseRifleQuad:
    flaring and fading over its life.
  - Fire_Gas: a fire's smoke, growing from nothing to the fire's size
    as it rises, fading (half as bright: many overlap).
  - FishInTank: each fish wanders to a place in the tank (the base's
    `Vect1`-`Vect2` box) and on to another, darting while startled (the
    base's `bUse`), facing the way it swims.
  - SwarmObject, Lava, Weather and the rest: by their velocity and
    acceleration (the swarm's scripts steer each by its acceleration).
- **Arcs** (`CTVE_ChainSpark`): `SpawnChain` makes `NumSegments` copies.
  `ChainSparkTick` lays them along a cubic Bezier from `StartLoc` to
  `FinishLoc`, leaving along `startPath` and arriving along
  `FinishPath`. Each tick the points between are jolted aside by up to
  `SpikeMag` (`EndSpikeMag` next to the ends), with a sine wave on top
  for `bSinVariation`. It grows (`bGrows`) and fades or glows up and down
  (`bFade`, `bCycleFade`). Each segment is the spark mesh stretched from
  one point to the next, `Thickness` wide.
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

`vm_save.cpp` saves a level being played and starts one from a save. The
file is Reluva's own: the original saves the level as a package, which
this doesn't read or write. The files take the game's names, in the
game's save folder (`Saves` in Reluva's folder for it): QuickSave.dsa,
and Save1.dss, Save2.dss... A file holds:
- **A header:** the map, a description ("QuickSave", or the level's
  title, "DS9 - Computer Core"), and when it was saved.
- **The objects:** the level's actors and every object they lead to that
  the level made, not the packages' objects, nor the engine's viewport,
  console and canvas. The map's own actors are written by their export,
  the rest by class and name. Each has its properties, its state, where
  its state code is, its Sleep and latent wait, the events it has
  disabled, and its timer. A map actor missing from the file was
  destroyed.
- **The level's records:** the actors' order, the player and game type,
  the time and ticks, what's touching, moves under way, falls,
  animation waits and layers, skin channels, attachments, bends and IK,
  the dialogue (how often each line's been said, the conversations
  running), and the counts behind made-up names.
- **The textures' properties:** the scripts change these (the HUD's
  places, the FLC textures' playing), and the engine keeps them across a
  load, its packages staying loaded.

Loading starts the map as usual, then puts the file's level in place
of `BeginPlay` and the player logging in. The player gets a new
viewport and console, and `bJustLoaded`, as the engine sets it. The
game isn't paused: it's saved paused (`ExecuteSaveGame`, `QuickSave`).

The game's side is all script:
- F6 (`QuickSave`) and the main menu's Save Game send `SaveGame`.
- The Load Game list asks `DrawLoadGameList`, which hands the HUD each
  game, newest first (`DrawLoadGameFile(Id, name, time)`).
- `LoadByFileID(Id)` gives the HUD its file, and the HUD travels to
  `?LOAD=Save1.dss`. The page loads that game's map and carries on
  from it.
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
(`vm_splines.cpp`). UnrealEd saves all it needs with each node:
- `TimeLine`: when the chain reaches it.
- `Duration` and `Length` of the segment after it.
- `Tangent`: on The Fallen's, the head's is exactly half the way to the
  next node, Catmull-Rom's.
- `AveSpeed`: the mean of this node's and the next's `Speed`.

So Reluva's reading is:
- **Timing.** Each node waits its `Pause`, then travels the segment, its
  speed changing evenly from its own `Speed` to the next's. `Duration`
  counts both: the pause plus `Length` at `AveSpeed` (CM10_SWK_L3's
  Sisko: 8 + 178/85 = 10.1), so the travel takes `Duration` less
  `Pause`. Counted all as travel, Sisko barely stopped where his camera
  looks over his shoulder, and had walked on before the shot.
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
  With a tween, it first tweens into the first frame over `TweenTime`,
  from the frame showing when it began (kept with the actor; one begun
  mid-tween starts from where that one started). While `AnimFrame` is
  below 0 the mesh is drawn blended from that frame to the first.
- A negative rate is scaled by the actor's speed (as the public
  UnrealScript documentation has it): each unit a second of `Velocity`
  plays `-Rate` times the sequence's speed, never slower than
  `LoopAnim`'s `MinRate` times it (`AnimMinRate`). No script of The
  Fallen's passes one.
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
- **Replacing one:** a layer started on a group whose layer shows holds
  the old one where it was, under the new one, until the new one has
  faded in (a combo's right punch into the left).
- **Keeping the actor's turn:** `ClearYaw`, `ClearPitch` and `ClearRoll`
  keep the part's first bone turned as it is under the layer about the
  actor's up, side and forward axes (the layer's turn less its twist
  about each); the bones below take the layer as it is. `CTV_Human`'s
  melee sets all three except when swimming, so a punch while running
  keeps the spine turned with the run and the arm punches.
- **Speed-scaled:** `C3DLoopAnim`'s negative rate and `MinRate` as an
  actor's own.
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
M04_KiraL1, once 2 is pressed, Kira holds the phaser rifle at her hip
facing where she faces, both hands on its grips, its muzzle 27 units
ahead of the right hand's.

Not done:
- The meanings above are Reluva's readings of how the scripts use the
  natives: the order of a bone's axes, the axes `C3DApplyRotation` turns
  about, the actor's axes for `C3DApplyWorldRotation`, and what reaches
  in an IK chain.
- `C3DRemoveRotation(Bone, bYawAxis, bPitchAxis, bRollAxis)` and
  `C3DApplyIKOnBasePose(IkChain, Val)` are Reluva's readings of their
  names, as no script calls them. The first takes the bone's turns
  (`C3DApplyRotation`'s about those axes, those parts of
  `C3DApplyWorldRotation`'s) back off. With the second's `Val` not 0,
  the chain's reach is done to the actor's own animation, before the
  layers go over it, rather than after them.
- Points on triangles turn with their triangle's bone, and points from
  the origin are in the skeleton's units ([mesh.md](../formats/unreal/mesh.md),
  `reluva-tool ue-points`). With them, Kira holds the phaser rifle at
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

**Speed**, measured without drawing (`ue-play`, M01_KiraL2, 20 s, 600
frames): 13 s in all, about 22 ms a frame.
- The scripts' ticks take 3.4 s.
- The page's own work (mostly the fire and water textures) takes 5.3 s,
  as it does when not playing.
- The rest is reading the live actors and the camera each frame, plus
  starting play and loading meshes once.

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
