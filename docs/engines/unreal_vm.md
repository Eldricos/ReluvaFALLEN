# The UnrealScript virtual machine

Status: **runs The Fallen's levels' start-up**, not the game. Every class
of the game loads (`vm.cpp`), every default property converts to its
type, and all 74 maps start: their actors made, the game type spawned,
the start-up events sent, then three seconds of ticks, with no crash
and, other than scripts finding no player (none has joined), one kind of
script error, a mistake in the game's own script (below). The Maps page
draws each level as its scripts leave it when play begins.

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
  (`Sleep`, and those not written yet, which wait a tick so loops round
  them don't spin) or stops. Each object's timer (`SetTimer`) and `Tick`
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
   its initial or auto state).
3. Spawned actors get `Spawned`, `PreBeginPlay`, `BeginPlay`,
   `PostBeginPlay`, `SetInitialState` as they're made; destroyed ones
   `Destroyed` (and their owner `LostChild`); `bStatic` and `bNoDelete`
   actors refuse to go.

`reluva-tool ue-run <game> <map | -all> [seconds]` runs it and reports
what scripts did: their log (repeats counted), errors, natives not
written yet by calls, None accessed by place. On The Fallen (October
2026), all 74 maps, 3 seconds each: 37 million calls, 0.3 s to start a
map on average (M01_KiraL1A, 3279 actors, 0.9 s), ticking at about a
tenth of real time on average but half of it on the biggest map.

What's left before play, by what `ue-run` reports:

- **No player.** Most of the None accessed is scripts looking at
  `Level.Player` or a player's pawn. A player has to log in
  (`GameInfo.Login`) and the engine fill `Level.Player`.
- **Natives not written** (31): The Fallen's own skeletal meshes
  (`C3DActorInit`, `C3DIsBoned`, `C3DGetAttachPtCoords`...), its
  `ListActors` iterator (an AI's list of known actors, not yet
  understood), `CheckFOV`, `GetRendererFlags`; and the engine's
  movement, collision and AI (`Trace`, `MoveTo`, `TurnToward`,
  `FindPathToward`, `PointCheck`, the path node iterators), which need
  collision against the level.
- **Speed**: values are copied freely and calls looked up by name; the
  biggest maps tick at half real time.
- **The game's own mistake**: `DS9_GrigariSentryDrone` (and its kin)
  `goto 'DetermineAction'` in their `DefensiveStance` state, where it's
  a function, not a label.

## In the Maps page

Loading a map runs its start and a second of play (`runLevelStart`),
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
