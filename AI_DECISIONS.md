# Minimum viable decision loop

The local `SceneWorld` is authoritative. Flecs owns creature Resource, Position,
Creature type/control, and persistent identity components. `Entity` is a value
snapshot. Chunk indexes contain IDs only, never duplicate live component data.
Jev chooses a candidate; the game owns execution and records the outcome.

See [Combat and disposition](COMBAT.md) for the shared attack action, species
defaults, local combat decisions, and nearby entity information supplied to Jev.

## Run

Build `JevSimulation`, then use:

```powershell
.\cmake-build-debug\JevSimulation.exe --ai-demo
.\cmake-build-debug\JevSimulation.exe --ai-stress
.\cmake-build-debug\JevSimulation.exe --ai-demo --window-smoke-test
```

The demo creates a disposable 2048x2048 world with a blue player, 20 orange AI
creatures, and obstacles. WASD pans, arrows move the player, Space pauses, and R
invalidates the decision session. The demo never overwrites your map.
`--ai-stress` always uses the offline fake provider, with 1,000 AI creatures and
60 seconds of simulated time. It reports world-cloning time, Windows private
memory, and decision counters. It has no window or network requirement.

Normal `--game` and editor Play run live Jev for AI-controlled creatures. Open
the editor's Entities tab, choose Jev AI or Player, and left-click walkable ground
to place a creature. Right-click removes one. Placement supports undo/redo and
map saving. Press Play to start decisions; authoring mode does not execute goals.
In Play/game mode, click a blue Player creature and use arrow keys to move it.
Human creatures initially loop the first three idle frames from `Units.png`,
with blue units for players and red units for AI. Species choose Units, Animals,
or Monsters; vocation mappings choose frames within that species' sheet. Cell hover uses the three
white frames in `Assets/UI/Interface.png`. Pause Play (or press Space in `--game`
and the demo), then hover a creature to see its type and Health. Current hitpoints
are centered inside the bar. `Resource::draw` draws the fill beneath the supplied
UI frame; the component holds no textures. Resource colors are red
Health, green Stamina, blue Mana/Thirst, gold Hunger, and purple custom pools.
These colors do not add unimplemented resources or Needs.
The HUD shows request, queue, fallback, and failure counts. Successful map
replacement and Stop discard the decision session; Pause cancels pending remote
work and freezes execution. Step advances existing goals without dispatching
network requests. Stop discards runtime changes.

## Configuration and proxy

### Creature definition editors

Open **Entities > Species / Vocation Editors**. These are authoring tools; Play
uses a snapshot of the saved definitions. **Prev/Next** browse definitions and
**New definition** allocates a stable enumeration ID. Human is species 0; every
species includes a Default subspecies. Add named subspecies in the Species editor.

- Species: choose the sheet, check resource pools, and use resource key/capacity
  fields to add custom pools or edit maxima. Health is required. Checkboxes for
  Thirst/Reproduction only configure pools; no Needs behavior is implemented yet.
- Vocations: create a named vocation and check each species that may use it.
  Click a species name to edit its individual mapping. Unassigned requires no
  vocation and uses the species' default frames.
- Sprite picker: choose AI row or Player row, then click the first 8x8 frame in
  the sheet. Both use the same starting column and frame count; rows may differ.
  Adjust frame count/FPS with minus/plus. Wheel scrolls vertically; Shift+wheel
  scrolls horizontally. Apply rejects mappings outside the selected sheet.
- **Apply + Save map** commits the complete draft and requests a map save.
  **Discard / Reload** restores current world definitions. Applied edits support
  undo/redo; a draft is not applied merely by switching editor tabs.
- Back under Tile Painting > Entities, click Species/Subspecies/Vocation to
  cycle valid choices. Left-click places a creature; Shift-click assigns these
  choices to an existing creature. Right-click removes one.

Definition changes reconcile existing creatures' resources: retained amounts
are clamped to new maxima, added pools start full, removed optional pools are
removed. Assignments that would invalidate existing creatures are rejected.
IDs are persisted numeric enumeration values, so additions need no C++ rebuild.
SpeciesComponent and VocationComponent are owned by Flecs, with snapshot copies
on Entity. New maps use version 7 for definitions and assignments; versions 1-6
still load with Human/Default/Unassigned. Goals and networking remain transient.

`ai-config.json` is versioned and loaded at session startup. Pass
`--ai-config path.json` to select another file. No hot reload is performed.

- `provider`: `proxy` (default, live Jev), `fake` (deterministic first-candidate selection),
  or `fallback-only` (seeded local selection).
- `bands`: five inclusive maximum chunk distances, minimum remote-decision
  intervals in simulation seconds, and service weights. The final distance is
  2147483647 (catch-all). Defaults are 0/1/3/7/far, 1/2/5/15/60 seconds, weights
  8/4/2/1/1.
- `batchSize`: maximum roster entries admitted/checked per tick (32).
- `requestsPerSecond`: world-session dispatch cap (10), measured in wall time.
- `timeout`, `backoffMin`, `backoffMax`: remote timeout and reconnect timing in
  wall seconds (2, 1, 30). A failed request is not retried.
- `queueWait`: simulation seconds before a queued creature receives fallback (2).
  Admission uses a rotating bounded roster, so this is checked when that entity's
  next scheduling slot arrives, rather than scanning every queued entity every tick.
- `perceptionRadius`, `candidateCount`, `payloadBytes`: defaults 8, 4, 8192.
  The byte cap is for the game/proxy request, not a tokenizer estimate or total
  provider-envelope size. The supported hard maximum is 8192 bytes.
- `moveStepSeconds`, `waitSeconds`, `rerouteAttempts`: 0.25, 1, and 2.
- `seed`: controls candidate and fallback randomness for a fixed world/ID order.
- `trace`: opt-in request/goal tracing to stderr, without credentials.
- `managedProxy`: true by default; the bridge starts and owns a persistent proxy
  on an available loopback port. No separate terminal is required. Set false to
  connect to a manually started proxy at `proxyUrl`.
- `proxy.mode`: `live` by default; `fake` exercises the complete transport offline.
- `proxy.port`, `proxy.maxConcurrency`, `proxy.requestsPerSecond`: local server
  defaults 8787, 8, and 40. These cap aggregate traffic across world sessions.

To exercise the actual bridge without contacting Jev, set `proxy.mode` to `fake`.
For an externally managed proxy, set `managedProxy` to false and start:

```powershell
node scripts/decision-proxy.mjs
```

The proxy binds only to 127.0.0.1 and defaults to live Jev. To override its mode
for an offline transport test:

```powershell
node scripts/decision-proxy.mjs --fake
```

Only that server loads `AI_GATEWAY_API_KEY` (or legacy `API_KEY`) from its
environment / local env files. The client bridge never reads those files or
transmits the provider key to the proxy. Use `--config path.json` on the proxy to
select its settings. In externally managed mode, keep the client's `proxyUrl`
port consistent with the proxy port. Managed proxies use ephemeral ports and
shut down with their bridge. Public hosting and authentication are not implemented.

Normal application startup no longer sends the old smoke-test inquiry. The
standalone live diagnostic is `--jev-smoke-test`; `--dry-run` and window smoke
checks remain offline. No live-provider request is needed by the test suite.

## World and persistence

New grids default to 2048x2048 cells at 8 world units/cell. Chunk size is fixed at
32 cells, with 64x64 chunks for a default world. Chunk distance uses Manhattan
distance to the nearest player-controlled creature, and no players means the
most distant band. Overlapping player areas do not create duplicate queue entries.

`spawnCreature`, `findCreature`, `moveCreature`, `removeCreature`, `creatureAt`,
`creaturesInChunk`, and `objectsInChunk` are the mutation/query boundary.
Creature movement preserves identity/resources, changes Position and occupancy,
and moves the ID between chunk buckets. Spawn and movement reject blocked or
occupied destinations. World commands can move directly to a destination; the
Move executor restricts its steps to a four-way path.

Creature positions use cell centers when spawned/moved by world commands. Their
fractional world coordinates survive save/load. `creaturePath` is a sparse
four-way search with a 4096-node budget, avoiding world-sized scratch arrays for
short AI paths; exhausting that budget counts as unreachable. `reachableCells`
uses a bounded four-way breadth-first search up to the perception budget. These
helpers do not change the existing generic navigation algorithms.

Maps write version 7: resources, creature identity/control/position, creature
catalogs, and species/subspecies/vocation assignments. Health remains in the common
resource section. Existing versions 1-6 and their original dimensions still load;
new-world defaults do not resize existing saves. Used GUID history covers
creatures, and load rebuilds spatial indexes. Terrain, creatures, and resources
participate in undo/redo and Play cloning. Goals, paths, request queues, and
network state are transient and are never serialized.

## Decisions and execution

`ActionRegistry` holds action rules, candidate generation, validation, and local
execution. `DecisionContextBuilder` collects typed JSON values from registered
component contributors. It does not parse or concatenate debug `fetchData()`
strings. The initial contributors are Health and Position, plus Range rules.

Requests carry version, world-session ID, request ID, entity decision revision,
self state, range, nearby entity dispositions/health/positions, shared rules, latest outcome, and up to four candidate goals.
Candidate IDs are local to the request. The C++ decision system retains the
entity mapping. Candidates include Attack and Harvest with a target GUID, Move
with a destination/path-step estimate, and Wait with a duration. Movement candidates are reachable within the perception
step budget; no full terrain map or path is transmitted. Oversized requests
shorten the nearby entity list, then remove extra candidates, retaining Wait and required rules. If required
context alone exceeds the cap, the system safely uses local Wait.

The proxy submits one boolean suitability question per candidate using the
existing Jev evaluation API. Highest score wins, with input order breaking ties;
these are rankings, not a normalized probability distribution. The response
contains protocol/session/request/revision and a selected candidate ID. C++
validates correlation, current position, action parameters, and reachability,
then packages the candidate into a `GoalRecord` with entity and goal identities.

A goal has action/parameters, source (`jev` or `fallback`), status (`pending`,
`running`, `succeeded`, `failed`, `cancelled`), and a result reason. Move retries
pathfinding after a blocked step; after two reroutes it fails with `unreachable`.
Wait succeeds after its simulation duration. Entity removal and explicit
interruption cancel execution. Only the latest outcome is included in the next
request. The immediately failed action/target is omitted from that next set of
candidates; this is a minimal short memory, not long-term learning.

There is one in-flight request per world session. Weighted round-robin selects
between distance-band queues, with oldest-ready ordering within each band.
Revision tickets suppress stale queue entries. Valid goals keep executing without
requests per step. Remote eligibility intervals never prevent local behavior:
entities on cooldown, offline, or waiting too long receive fallback goals through
the same executor. Late responses are rejected after pause, reset, replacement,
position changes, or a superseding decision. The persistent Node process is
reused while healthy; timeout/cancellation tears it down and the next eligible
request starts a replacement. Its job object handles application shutdown.

## Extending and measuring

Register component contributors for compact state/rules. Register new action
definitions to add candidate generation, parameter validation, and execution.
The proxy can score extension actions with supplied rules without understanding
their local implementation. Keep a Wait action available as the safe baseline.
Future Needs can rank/filter opportunities before the payload cap is applied.

`DecisionSystem::metrics()` reports per-band requests, request bytes, aggregate
latency and queue seconds, stale responses, failures, fallback assignments,
succeeded goals, and cancellations. Console output reports metrics on demo/game
shutdown. Enable `trace` to correlate request IDs with goal starts/results.

Run `ctest --test-dir cmake-build-debug --output-on-failure` after building all
targets. Tests cover real JSON parsing, fake provider choice, persistent C++/Node
pipes, proxy limits, failure recovery, outcome feedback, weighted scheduling,
1,000 offline entities, full-size saves, creature history, and legacy behavior.
The standalone stress and graphical smoke commands are additional opt-in checks.

The dense grid and full Play clone remain the largest memory costs. Treat the
reported Debug-build measurements as a local baseline, not an MMO capacity
claim. A future shared-world server should own this same world/decision service;
clients must not run duplicate AI for entities they merely observe.

## Recorded local baseline

The Windows MinGW Debug stress run (1,000 AI creatures, 60 simulated seconds,
fake provider) completed in about 45.7 seconds. Full-world cloning took 515 ms.
The dense grid alone is 256 MiB; measured process private memory was 261 MiB
before cloning, 520 MiB with the clone retained, and 264 MiB after the stress run.
These retained-memory readings are not a peak measurement: the temporary
WorldDocument also holds a full grid while cloning. The run dispatched 556
remote-style fake decisions; remaining activity used local fallback. Dense
crowding produced legitimate unreachable-goal results, exercising recovery.

All 16 CTest cases passed, including the C++/Node/proxy integration. Both the AI
demo and existing editor Play/Pause/Step/Stop hidden-window smoke checks passed.
The main executable integration also passed entity placement/undo/redo and
Play/Pause/Step/Stop checks. Two live Jev decisions passed through the same C++
transport and automatically managed proxy used by the main executable; observed
provider latency was 644 ms on the first request and 217 ms on the second.
