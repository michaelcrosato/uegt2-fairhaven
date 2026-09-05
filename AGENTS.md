# Fairhaven (UEGT2) engineering guide

Read this before changing anything. [Docs/Architecture.md](Docs/Architecture.md)
explains how the project fits together; this file is the working contract.

## Project contract

- Native Windows Unreal Engine 5.8 C++ project, Win64 / DX12 / SM6, targeting an
  RTX 3060-class GPU at 1920×1080.
- **The world is generated, never hand-authored.** The map is a build artifact.
  Anything you place by hand in the editor is destroyed by the next content
  build. Put it in a stage instead.
- C++ for durable runtime systems, Python for authoring content. No Blueprints
  and no binary UI assets: the whole project should stay diffable.
- Keep it launchable after every substantial change.
- New gameplay features need a stable entry in [Docs/Features.md](Docs/Features.md),
  an independent maintainer off switch and a tested disabled path. Add a player
  toggle when useful. Disabling must preserve saved data and baseline play.

## Standard commands

```powershell
./Scripts/Setup-Project.ps1                     # fresh clone -> playable build
./Scripts/Build.ps1 -Target Both                # ALWAYS both targets
./Scripts/Build-Content.ps1 -Stages all         # rebuild the world
./Scripts/Build-Content.ps1 -Stages nature      # rebuild one stage
./Scripts/Build-Content.ps1 -Stages npc         # re-roll the population + road graph
./Scripts/Test.ps1                              # automation tests
./Scripts/Smoke-Crossing.ps1                    # ordinary walking across the lower bridge, both ways
./Scripts/Smoke-ContractWalk.ps1                # complete survey circuit, live needs and clock
python Tools/Python/test_street_clearance.py     # street frontage and junction clearance, no editor
./Scripts/Package.ps1                           # playable build
./Scripts/Screenshot-Tour.ps1                   # registered viewpoints -> PNG
./Scripts/Screenshot-Tour.ps1 -Menu             # menu + settings -> PNG
./Scripts/Screenshot-Tour.ps1 -ExtraArgs '-UEGT2CaptureDialogue'   # the talk panel
./Scripts/Screenshot-Tour.ps1 -ExtraArgs '-UEGT2CaptureLife'       # eat, wash, sit, work
./Scripts/Screenshot-Tour.ps1 -ExtraArgs '-UEGT2CaptureSquareWashrooms' # all four square washrooms
./Scripts/Fly-Soak.ps1 -Minutes 10                                 # god mode, ten minutes, every hitch
./Scripts/Preview.ps1 -Stages lighting          # build + package + screenshot
python Tools/Python/check_meshes.py             # every catalog mesh, no editor, a few seconds
python Tools/Python/test_pipeline.py            # stage selection + geometry regressions, no editor
python Tools/Python/test_privy_placement.py     # four clear square washrooms and their approaches
./Scripts/Tests/Test-Verification.ps1            # script failure handling, no engine
./Scripts/Tests/Test-ResolveEngine.ps1           # engine discovery, no engine launch
python Tools/Terrain/generate_terrain.py        # re-roll terrain (+ PNG previews)
python Tools/Audio/generate_audio.py            # re-generate sounds
```

When `LocalBuilds` contains more than one package, pass
`-PackagedExecutable <path-to-Binaries\Win64\UEGT2.exe>` to the packaged
smoke, screenshot, fly-soak or needs-reminders wrapper. They fail closed rather
than selecting an archive from its executable timestamp.

## Verification, in order of how much it has actually caught

1. **`-Target Both`.** The Game target rejects editor-only APIs the editor build
   accepts. `ADirectionalLight::GetComponent()` compiled fine in the editor and
   broke the game target.
1b. **`python Tools/Python/check_meshes.py`.** Builds every mesh in the catalog
   with the `unreal` module stubbed out and checks bounds, vertex buffers,
   triangle indices, finite attributes, degenerate triangles, UV0 area, window
   apertures, doorway clearance and interior fit. A few seconds, no
   editor. It will not tell you anything about materials, lighting or how a
   thing looks - only that the geometry is what you meant.
2. **Look at a screenshot.** `./Scripts/Preview.ps1`. Several bugs here produced
   a completely clean log and a completely broken image: inverted triangle
   winding, an unconnected BaseColor, blown-out exposure.
2b. **`-UEGT2CaptureLife`.** Walks the player up to one amenity of each kind,
   uses it through the real interaction probe, and logs the needs and the purse
   either side. It is the only check that covers the whole path the player
   takes rather than the pieces of it: the arithmetic tests cannot tell you
   that pressing the key does anything.
3. **Package it.** The cook is the only place material shaders actually compile
   and the only place bad physics collision is reported. `Build-Content.ps1` and
   `Package.ps1` both fail the build on a material that does not compile.
4. **`./Scripts/Test.ps1`.** Guards materials, vertex colours, world composition,
   and the whole NPC behaviour model - the routines, the rules that override
   them, the speech pools and the baked population.
   `UEGT2.NPC.*` and `UEGT2.Economy.*` need no map and run in seconds. The
   second of those simulates three whole days per trade with the needs and the
   purse in the loop, which is the only way a wage change that slowly starves
   the bakers is visible before you play for an hour.
4b. **`./Scripts/Fly-Soak.ps1`.** Flies god mode round a circuit of both
   settlements for ten minutes and logs, every second, the worst frame in that
   second next to the object count, the memory, the hour and how many
   inhabitants are near. It exists because the thing it found - a four gigabyte
   runaway allocation inside A* that froze the game for twenty seconds and then
   killed it - produces a clean log, a clean test run and a clean screenshot.
   Nothing else here runs for long enough to see it.

   When it reports a hang, arm the engine's own hang detector to find out
   where: put `[Core.System]` `HangDuration=8.0` and `HangsAreFatal=False` in
   `Config/DefaultEngine.ini`, repackage, and the game logs the full callstack
   of whichever thread stopped answering. That is how the A* freeze was found
   and it took one run. Take it back out afterwards - eight seconds is under
   what a cold map load can take.
5. **Read `Saved/Logs/ContentBuild.log`** when a stage misbehaves. The build
   script echoes only the `[UEGT2]` lines; the full log has everything.
6. **Read the population report.** Any run logs one `LogUEGT2NPC` line twelve
   seconds in: how many inhabitants exist, how many are out, how many are
   walking, how many are near the player, and the mean frame rate. "769 placed,
   none of them anywhere near the player" is what a movement bug looks like from
   the outside, and it renders as a perfectly clean log over an empty town.

## Traps already paid for

Do not undo these without understanding why they are there.

- **Triangle winding** (`meshkit._emit`): geometry is authored right-handed and
  the indices are swapped on the way out, because Unreal takes the opposite
  winding. Undoing this makes all two-sided foliage render pure black.
- **Wind weight is in UV1.x**, not vertex alpha. Every `VertexColor` output pin
  is named `""`, so only the RGB pin is reachable by name.
- **UV0 must give every face a nonzero area.** `meshkit._mesh_uvs` uses the
  face's dominant plane. A single XY projection collapses vertical faces and
  leaves MikkTSpace with degenerate tangent bases; changing UV0 must preserve
  UV1 wind weights, normals and winding.
- **A window's whole aperture must be clear.** Solid frame boxes covered the
  translucent panes, and separate sill/lintel blocks filled stacked windows
  back in. Use perimeter rails and `meshkit.wall`'s union of rectangular
  openings. `check_meshes.py` traces through actual panes; `test_pipeline.py`
  covers stacked, overlapping and clipped openings, winding and doors.
- **Lightmap UV generation is off** (`ConfigureGeneratedMesh`) because it would
  overwrite UV1.
- **Auto-exposure min/max are EV100 stops**, not multipliers, because
  `ExtendDefaultLuminanceRange` is on. Small values there blow the image to pure
  white. See the comment in `lighting.py`.
- **`MaterialEditingLibrary` returns `False` for a bad pin name** instead of
  raising. `materials._to` / `_link` warn loudly on failure; keep them.
- **Physics needs `collision="simple"`** in the mesh catalog. Complex-as-simple
  cannot be simulated and the cook fails.
- **Never pass the `.uproject` to a packaged build.** It makes the game look for
  uncooked content through a Zen server and fail to start.
- **`-AdditionalCookerOptions=-SkipZenStore` in `Package.ps1`** writes cooked
  payloads to disk so staging does not depend on a temporary Zen cook store.
  UE 5.8 ignores the old `-nozenstore` argument. Pak/IoStore still produces the
  self-contained game; Zen's derived-data cache is a separate concern.
- **UAT needs `-UbtArgs=-WaitMutex` to wait for another project build.** Without
  it, packaging's child UBT fails with `ConflictingInstance` if another local
  project holds the engine's shared build lock. UAT labels that exit code 10
  `Error_SDKNotFound`; read the actual UBT error before diagnosing an SDK.
- **Unreal's `-script=` argument processes backslash escapes.** Pass Python
  script paths with forward slashes or `\u` in the project path corrupts them.
- **`ctx.set_prop` warns instead of failing** on an unknown engine property.
  Every warning it prints is a real TODO. Check the log after a content build.
- **File-local helpers go in a named namespace, not an anonymous one.**
  `SUEGT2Menu.cpp` ends its style block with a file-scope `using namespace
  UEGT2Menu;`, and a unity build concatenates it ahead of other files in the
  module. An anonymous namespace puts its contents at global scope, where
  `Ink`/`Muted`/`Accent` then collide with the menu's (C2872). This is invisible
  locally, because adaptive unity compiles *modified* files on their own - it
  only breaks on a clean checkout. Verify with `-DisableAdaptiveUnity`.
- **The day/night cycle is force-frozen during captures.**
  `AUEGT2SkyController::BeginPlay` sets `DayLengthMinutes = 0` when
  `UUEGT2CaptureSubsystem::IsCaptureRequested()` or `IsWalkSmokeRequested()` is
  true. Without it `Screenshot-Tour.ps1` produces a different image every run.
- **The map wins over C++ defaults on placed actors.** `lighting.py` serialises
  `day_length_minutes = 0.0`, so the cycle is switched on by promoting a zero
  value in `BeginPlay`, not by changing the property default.
- **`polyline_field` must be given a margin.** Without one it scans the entire
  grid per road segment: at 4033 x 4033 that is 16 million samples times roughly
  1500 segments, allocating six full-size temporaries each time, and the road
  pass does not finish. Windowed to each segment plus a margin it is 177 seconds
  for the whole terrain. The margin must exceed the widest falloff the caller
  applies, or distant samples silently keep distance 1e12.
- **`is_street` includes the Newhaven grid.** Anything that means "town street"
  has to filter on `not is_city` too, or it will lay cottages down city avenues.
- **Fit bridges across the full rendered river width.** At the lower crossing,
  the water spans about 40 m along the road centreline but 49 m across the whole
  deck footprint. `bridges.py` clips the actual water triangles and fits dry
  road landings. The river has no collision: a walking check must verify bridge
  floor support, or it can pass while the player walks along the riverbed.
- **Scatter actor class does not identify stage ownership.** Nature and town
  fences both use `AUEGT2ScatterField`. Nature replaces only its opted-in fields
  or legacy `Scatter ` labels; fences survive a nature rebuild and keep their
  own draw distances. Foliage settings scale authored cull distances on opted-in
  fields, never already scaled values.
- **Fixed attempt counts do not survive a bigger map.** Scatter loops written as
  `range(900)` quarter in density when the extent doubles. Scale by area.
- **An NPC ground trace starts at +90 cm, below knee height.** A market awning
  is 250 cm up, a stall counter 154, a kiosk body 260, a bus shelter bench 230.
  A trace that starts above those finds the awning first and snaps the villager
  onto it; they walk off the edge, drop, come back under and pop up again. That
  is what "floating in the sky" and "bouncing" look like.
- **NPC destinations are ground-sampled in `RepathTo` and nowhere else.** An
  anchor is one point and a crowd is spread around it horizontally, so
  inheriting the anchor's height leaves most of the crowd in the air on any
  ground that is not flat.
- **Walking height belongs to the new horizontal position.** Interpolating
  with the pre-step distance leaves feet behind on slopes, especially at the
  far tier's half-second interval. A periodic ground correction must move the
  actor and rebase the remaining segment, or the next tick overwrites it.
  `UEGT2.NPC.Grounding.*` checks a slope and raised ground under an awning.
- **A crowd's capacity is the number of distinct anchor points, not the spread
  radius.** Five market stalls served ninety villagers and produced one writhing
  mass; thirteen stalls and twelve benches produce a market.
- **A missing city landmark must never fall back to a town one.** Newhaven's
  fountain silently failed to place, so every city routine pointing at "Plaza"
  resolved to the *town* square and the city's couriers and constables walked
  130 km. From inside the city that reads as "the city is empty".
- **`Placer.place` rejects silently.** It returns None on overlap and the
  calling stage usually ignores the result, so a stage can log success and place
  nothing. The npc stage now warns about empty anchor sets and unclaimed actor
  labels for exactly this reason.
- **Required entrances need protection across content stages.** Town's four
  square privies reserve their bodies and front approaches, but gameplay used
  to scatter a carryable prop into one of those entrances afterwards. Pickup
  placement now respects the tagged privy reservations. Keep the capsule-based
  `UEGT2.Content.SquarePrivies` check: an earlier stage's clean placement does
  not establish that the completed map is still clear.
- **An NPC's mesh must not be its root component.** The walk cycle is a relative
  offset applied to the mesh every frame, and a relative move on the *root* is a
  world move: every NPC inside LOD range teleports to the world origin, which is
  under the town square. `AUEGT2NPCActor` puts a plain scene root above the mesh
  for exactly this reason. The symptom is a town that is full when frozen for a
  capture and empty the moment anything ticks.
- **NPC ground traces query `ECC_WorldStatic` by object type, not the Visibility
  channel.** Every NPC blocks Visibility so the interaction probe can find them,
  and a channel trace lands one NPC on another's head.
- **Never hold a pointer into a container across a write to that container.**
  `FindPath` took `const double* CostHere = BestCost.Find(...)` and then called
  `BestCost.Add(...)` further down the same neighbour loop. The Add rehashes
  the map and frees the block, so every neighbour after the first reallocation
  read freed memory as "the cost so far". The damage was not a wrong route: a
  garbage cost breaks the invariant that a node's parent costs less than the
  node, which lets the parent links contain a *cycle*, and the walk back from
  the goal then loops forever appending to an array - four gigabytes in eight
  seconds, then the allocator asserts. It presented as "god mode flying freezes
  after a few minutes", because flying is what makes hundreds of inhabitants
  change tier at once and repath together, which is what rolls the dice often
  enough to hit it. Copy the value.
- **The player and the town share one ledger.** `UEGT2AdvanceLife` is the only
  place elapsed time changes needs and money, and both `AUEGT2NPCActor::AdvanceNeeds` and
  `UUEGT2NeedsComponent` call it. Do not give either a rate table of its own:
  the point of the player having needs at all is that they are the same needs,
  and a second table drifts silently. `UEGT2.Economy.LivingWage` runs every
  trade through three closed-loop days and fails if the numbers stop adding up.
  One-off rewards use `UEGT2TryCredit` in the same ledger, with bounded purse
  validation; never invent elapsed work hours or use the dev setter to pay one.
- **Old checkpoints can omit their schema tag.** UE's default delta serializer
  omitted schema 1 because it matched the class default. Increasing the C++
  default alone makes old saves look current. `ProgressSave::Serialize` begins
  loading at the legacy baseline and writes all new fields explicitly. Keep
  the genuine schema-1 fixture; a new object relabelled as schema 1 misses this.
- **Auto-walk needs a fresh raw press, not just an Enhanced Input edge.** The
  installed engine flushes a held key by synthesizing a release, then can rebuild
  a press from an OS repeat. Pausing also resets action trigger state. An action
  bound only to `Started` can therefore restart walking on menu close; a fast
  release/repress can instead remain `Triggered` without another `Started`.
  The controller's one-tick raw-press token authorizes exactly one action and
  survives a real quick tap, while repeats never authorize it. Keep the held-key,
  quick-tap and focus regressions in `UEGT2.Player.AutoWalk.*`.
- **Schedule slices do not determine elapsed life time.** The director records
  each NPC's last charged world hour. Multiplying the latest slice interval by
  six overcharged small populations and uneven frames. Registration, crowd
  suppression, clock changes and frozen captures must preserve that accounting;
  `UEGT2.NPC.Director.*` tests the real subsystem.
- **An amenity is a place, not a prop.** `AUEGT2Amenity` is an invisible query
  volume standing on an anchor point, and it must stay that way. Converting the
  bench, privy or stall props into interactable actors changes their collision
  object type from `WorldStatic` to `WorldDynamic`, and the NPC ground trace
  queries `ECC_WorldStatic` **by object type** - so the whole square would stop
  being standable on the same day the benches became usable.
- **NPCs do not block the player.** Query-only collision, Visibility alone. The
  player starts in the town square where the crowd is thickest, and a solid crowd
  there means getting wedged between four villagers - and a packaged walk smoke
  that fails because somebody stood in front of the pawn.
- **Shared NPC anchors are picked from the closest few by seed, not by rank.**
  The town has five market stalls and ninety villagers; "nearest" sends all
  ninety to the same one and produces a single writhing mass of people.
- **The NPC director tracks the player's view point, not the pawn.** They differ
  during a screenshot tour, which parks the pawn at the player start and flies a
  separate camera around.
- **Screenshot tours freeze the population**, for the same reason they freeze the
  sun. `-UEGT2LiveNPCs` opts out and logs every line spoken; it is not
  reproducible, which is why it is not the default.
- **Auto exposure has to follow the time of day, or night renders pure black.**
  `lighting.py` bakes `auto_exposure_min_brightness = 10.5` *EV100* into the post
  process volume - that is a daylight floor. A moonlit scene sits near EV 0, so a
  fixed floor clamps it about ten stops too dark and the screen is black with a
  completely clean log. `AUEGT2SkyController::ApplySky` slides the min/max EV
  window down with the sun; the day values match what lighting.py writes, so
  noon is unchanged. Brightening the moon alone does not fix this.

## Adding things

| To add | Do this |
|---|---|
| A mesh | Generator in `gen_nature.py` / `gen_town.py`, one line in `meshbuild._catalog()` |
| A plant or rock species | A `Species(...)` entry in `nature._rules()` |
| A usable object | Subclass `AUEGT2InteractableActor`, implement `OnInteract`, place it in `gameplay.py` |
| A setting | `UPROPERTY(Config)` on `UUEGT2GameUserSettings` + getter/setter + a row in `SUEGT2Menu.cpp` |
| A dev mode control | Getter/setter on `UUEGT2DevModeSubsystem`, then a row in the matching `BuildDev*Tab()` |
| A weather preset | An `EUEGT2Weather` entry + a row in `UEGT2WeatherTable::Presets`; the tests check the table |
| A city building | A generator in `gen_city.py`, one line in `meshbuild._catalog()`, one entry in `city.CITY_BUILDINGS` and its ring |
| More map | `COMPONENT_COUNT` in `world_config.py`; it must stay `count * sections * quads + 1` |
| A colour | `Tools/Python/uegt2/palette.py` — it is the only place colours live |
| A world feature (road, region, river) | `Tools/Terrain/world_config.py`, then re-run the terrain script |
| A screenshot viewpoint | `UUEGT2CaptureSubsystem::GetTour()` |
| A trade, or a change to one's day | A routine in `UEGT2NPCRoutines.cpp`, plus a `_ROLE_LOOK` row in `npc.py` |
| Somewhere the player can eat, wash, sit, sleep or earn | An `EUEGT2AmenityKind` entry, its activity in `UEGT2ActivityForAmenity`, and a placement loop in `gameplay._place_amenities` |
| A price or a wage | `UEGT2PriceFor` / `UEGT2WagePerHour` in `UEGT2NPCTypes.cpp`; `UEGT2.Economy.LivingWage` checks the result still feeds the town |
| An animal | A generator in `gen_fauna.py`, a `meshbuild._catalog()` line, a species routine |
| Something an NPC says | A pool in `UEGT2NPCSpeech.cpp`; the tests check every pool is filled |
| A rule that overrides a routine | `ResolveActivity` in `UEGT2NPCRoutines.cpp` |
| A content stage | A module in `uegt2/`, then register it in `build_content.py` |

## Style

- Match the surrounding code. Comments explain *why*, especially where an engine
  behaviour is surprising; the code already says what.
- One log channel per system (`UEGT2LogChannels.h`). Never `LogTemp`.
- Python: no numpy inside Unreal (it is not there). Heavy maths goes in
  `Tools/Terrain` or `Tools/Audio`, which run under system Python.
- Determinism matters: use `meshkit._SmallRng` or `ctx.Rng` seeded from
  `world_data.seed`, never `random`. The same seed must give the same world.

## Git

- Work on focused branches; keep `main` buildable.
- `Content/` holds the generated `.uasset` binaries and IS committed (~5 MB), so
  the materials, meshes and audio come with a clone. **The map does not.** See
  below.
- `Tools/Terrain/Output/` and `Tools/Audio/Output/` are NOT committed. Recreate
  them with `Scripts/Setup-Project.ps1`, or the two `generate_*.py` scripts
  directly; the content build needs them and will say so if they are missing.
- `Binaries/`, `Intermediate/`, `Saved/`, `LocalBuilds/`, `Build/` and
  `DerivedDataCache/` are ignored. `Build/` matters: the cook writes file-open
  order logs there containing absolute user paths.
- Never commit credentials or absolute engine paths.

### The map is not committed

`Content/Maps/L_Fairhaven.umap` is a **build artifact** and is gitignored.

It used to be committed at ~54 MB, on the reasoning that a clone should be
immediately usable. Growing the landscape to 4033 ended that: the map is now
~195 MB, which is past GitHub's **100 MB per-file hard limit**, so it cannot be
pushed at all. This is option 1 of the two that used to be listed here, applied
to the file that actually grows rather than to all of `Content/`:

- everything else under `Content/` is small and stable (~5 MB of materials,
  meshes and audio) and stays committed;
- the map is rewritten *whole* by every content build, so committing it added
  its full size to history every time.

Git LFS was the other option and was rejected for the reason already noted: on a
*public* repo every clone draws on the owner's LFS bandwidth quota, and at
195 MB a handful of clones would exhaust the free tier.

**What a clone has to do:** run `./Scripts/Setup-Project.ps1`, which generates
the terrain, builds both targets, rebuilds the world and packages it. Allow
time for mesh and shader generation as well as compilation; the scripts report
their elapsed time. `./Scripts/Build-Content.ps1 -Stages all` rebuilds both the
catalog assets and the map once the terrain output exists. Select individual
stages when only part of the world needs rebuilding.

If the map ever needs to ship with the repo again, LFS is the route, and it is
one `git lfs track "*.umap"` away.
