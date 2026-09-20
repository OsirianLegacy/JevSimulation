# Jev inquiry smoke test

Calls `typesafe-ai/jev` through Vercel AI Gateway from Node.js 22+. No additional packages or deployment are required.

## Setup and run

1. Obtain an **AI Gateway API key** from your Vercel dashboard (AI Gateway > API Keys).
2. Set `AI_GATEWAY_API_KEY` in `.env` or `.env.local`. `API_KEY` is also accepted for the existing local setup. Both files are ignored by Git. Existing environment variables take precedence over file values; `.env.local` loads before `.env`. If both key names are set, `AI_GATEWAY_API_KEY` is used.
3. From this project directory, run:

```powershell
node scripts/test-jev.mjs
```

The script sends one synthetic game scenario with two boolean questions. It checks that Mira is identified as a guard (probability >= 0.8) and not a merchant (probability <= 0.2). It prints probabilities and elapsed time, logs `[WARNING] Jev responded properly` on success, and logs `[ERROR] Jev test failed` with exit code 1 for HTTP errors, timeouts, invalid responses, or wrong answers. There are no automatic retries. A live call uses your Gateway account.

This is a small connectivity and correctness check, not a model quality benchmark. Thresholds are smoke-test expectations, not calibrated confidence guarantees. Edit `inquiry` and `validateResponse` together to test different known facts.

## Run in CLion

Select **JevSimulation** in the Run configuration selector and click **Run** (Shift+F10). CLion builds and launches the C++ application. The application starts Node as its child, runs `scripts/test-jev.mjs`, and shows the script's WARNING/ERROR logs in the same console. CMake locates Node automatically; override `JEV_NODE_EXECUTABLE` in CMake settings if needed. The script loads your local key automatically.

The C++ application opens a raylib 6.0 window and runs a 60 FPS game loop until `WindowShouldClose()` (window close button or Escape). The Jev inquiry starts once, asynchronously, and its completion is polled each frame. Results appear in the console and the window remains open after the result. Node exits when its test completes; closing the window while the request is pending cancels that child through its Windows Job Object. Forced application termination also stops its Node child. The launcher currently supports Windows.

CMake downloads the official raylib `6.0` tag through FetchContent on first configuration and links it statically. Add simulation updates and drawing inside the loop in `main.cpp`; the Node owner stays alive around the loop. Building alone does not send a request. The separate **Jev Node** configuration is available for running the script directly.

## World grid and camera

The world contains 1000 columns by 1000 rows, with **8x8 cells** (8,000 by 8,000 world units), matching the source tiles. Coordinates start at `(0, 0)` in the top-left; X increases right and Y increases down. The camera starts at the center at 3x magnification with crisp point-filtered tiles. Hold **WASD** to pan at 600 world units/second; diagonal movement is normalized. Scroll over the world to zoom toward the cursor. The camera clamps to the world edges and adapts to window resizing. Hover a cell to see its coordinates and walkability.

`Grid.h` provides checked and optional cell access, row-major index conversion, world/cell conversion, cell centers and bounds, fill/reset, clipped rectangular fills, layer painting/erasing, walkability, and four/eight-way neighbor queries. Neighbors are geometric adjacency (diagonal queries can cross blocked corners); they are not a pathfinding policy.

Each cell has **Ground, Walls, Objects, and Entities** layers. Every layer holds its own tileset ID and source column/row. A new or cleared cell has no tiles and is **not walkable**. Ground enables movement when Walls and Entities are empty and the Objects layer is empty or contains an open Door. Containers, Gatherables, and closed Doors prevent movement. Removing a blocker restores movement only if Ground remains and no other blocker remains. Walkability is derived rather than stored as a flag, so all API edits obey these rules. The old `setWalkable` API is replaced by `paint` / `erase`.

## Tileset editor

- **F1** opens/closes the left editor. Painting is active only while it is open.
- The top dropdown selects a PNG from `Assets/Tilesets` (discovered at startup). Overworld is selected initially; Structures and Walls are also available. New sheets must align to an 8x8 tile grid.
- Select **Ground**, **Walls**, or **Objects**, then click a tile in the sheet. For Objects, select **Paint** and choose Door, Container, or Gatherable. The Entities tab remains reserved.
- **Drag over the tilesheet** to select a rectangular group of 8x8 tiles (dragging in either direction works). A single click selects one tile. The outline and brush preview show the full selection. Changing sheets resets it.
- **Left-drag over the map** paints the selection with its tile arrangement intact, anchored at its top-left. Strokes repeat on a grid matching the brush dimensions, so neighboring tiles are not smeared together. **Right-drag** erases the same rectangular footprint on the active layer. Stamps clip at world edges; palette drags cannot paint the map. On Objects, each cell gets its own GUID and occupied cells stay unchanged.
- Scroll over the atlas to move vertically; **Shift+wheel** scrolls horizontally. Both axes also have draggable scrollbars, and **-/+** adjust atlas magnification.
- Tile selection and panel clicks cannot paint through onto the world. Each painted cell retains its own sheet reference when you switch sheets.

Use **Save / Ctrl+S** to save the map to `Maps/world.jevmap`, or **Load / Ctrl+O** to restore that file. Shortcuts work even when the editor is closed. The last saved map loads automatically on startup; manual loading replaces unsaved edits. Closing does not automatically save, so save your changes before exiting. The status bar and console report success/failure. PNG assets are never modified.

The versioned sparse map file preserves dimensions, 8x8 cell size, origin, and all four tile layers. Only painted entries are stored. Tilesheets are referenced by filename and resolved to current IDs on load, so adding or reordering sheets does not change existing paintings. Missing sheets, out-of-range tiles, unsupported versions, duplicate records, and malformed files fail without replacing the current grid. Save writes a temporary file beside the destination and replaces the previous save only after a successful write. There is currently one save slot; copy `Maps/world.jevmap` externally to keep another map.

```cpp
grid.paint({10, 10}, GridLayer::Ground, TileRef{0, 2, 1});
grid.paint({10, 10}, GridLayer::Walls, TileRef{2, 0, 0});
grid.erase({10, 10}, GridLayer::Walls); // Ground is now walkable again.
```

Rendering only traverses cells intersecting the camera view and draws shared grid lines once, with stronger lines every ten cells. `WorldCamera` separates WASD input from testable movement and viewport calculations. Add game updates before `BeginDrawing()` and world drawing inside `BeginMode2D()` in `main.cpp`.

## Item and object-type creation

While **F1 editor mode** is open, the top bar switches between **Tile Painting**, **Item Creation**, and **Object Types**. Creation screens capture keyboard/mouse input so editing cannot pan the camera or paint the map.

- **Item Creation:** enter a unique ID (for example `iron_ore`) and display name, then **Create + Save map**. This adds a reusable item definition and saves the current map, including other world edits. In the object inspector, click **New stack** to cycle through catalog items, then **Add item stack** to copy that item's ID/name into a quantity-1 draft row. Apply to commit the contents.
- **Object Types:** enter a unique ID/name and choose **Door**, **Container**, or **Gatherable** as its base behavior, then **Create + Save map**. In Tile Painting, select Objects > Paint > **Custom type...** to select it. The popup scrolls when more types are available. Named types inherit their base movement, vision, opening, and contents rules.
- IDs and names must be nonblank and at most 255 bytes. IDs are unique within each catalog. Existing definitions are immutable in this version; create another ID for a different definition. There is no delete or rename UI yet. **New / Clear draft** resets the creation form.

Definitions live with the world in `Maps/world.jevmap`, not in the PNG tilesheets. The normal save/load status reports whether the write succeeded; Ctrl+S retries a failed write. The catalogs reload at startup and through Ctrl+O. Clearing grid cells preserves the catalogs. Changing modes/F1 discards unfinished creation drafts.

Version 3 adds the two catalogs and persistent named-type references. Version-1 and version-2 maps still load with empty catalogs and retain their previous tile/object behavior. Loading rejects duplicate definitions, unsupported base behaviors, and missing/incompatible type references before replacing the current world.

## Objects and items

Each occupied Objects cell references one persistent 128-bit UUID. SceneWorld owns the Flecs entities; `at()` and `tryGet()` expose read-only cells. Use `placeObject`, `objectAt`/`findObject`, `moveObject`, and `removeObject` to manage them. A move retains identity and contents; painting an occupied cell leaves it unchanged. Erasing destroys the instance and contents. Generic Objects-layer painting creates an empty Container; use `placeObject` to specify another type. Cell/region replacement rejects supplied GUIDs and removes displaced instances.

In **F1 > Objects > Inspect**, left-click a world object to edit it. The inspector shows its GUID and fixed type. Toggle Open for Doors/Containers. Containers and Gatherables support item stacks with an ID, display name, and positive quantity. Click **Add item stack**, click a field to type, and use Backspace to edit. Scroll the properties area to reach additional rows. **Apply** validates and commits the draft; **Cancel** restores the object's current data. Switching selection/mode or closing the panel discards the draft. Apply before saving. Typing in a field captures WASD so the camera stays still. Right-click in Inspect erases the object.

Only Container objects report `isContainer() == true`; Gatherables also hold resource contents but cannot be opened. Emptying contents does not remove an instance. Container open state is stored without restricting editor access. Item stacks remain separate rows; no capacity, harvesting, or player-inventory transfer is implemented yet.

```cpp
auto chest = grid.placeObject({10, 10}, TileRef{0, 2, 1}, ObjectType::Container);
grid.addItem(chest, Item{"wood", "Wood", 20});
grid.updateItem(chest, 0, Item{"wood", "Wood", 30});
grid.moveObject({10, 10}, {11, 10}); // Same GUID and contents.
const auto object = grid.findObject(chest); // Optional read-only value snapshot.
```

Saves retain object GUIDs, properties, contents, and any custom type reference. Version-1 maps still load: old object tiles become closed, empty Containers with fresh UUIDs. The next save writes version 4. Stack text is limited to 255 bytes and contents to 1024 stacks per object; quantity ranges from 1 to 4294967295. Invalid edits and malformed loads preserve current data. Object tile artwork stays the same when a door opens.

## Shared GUID allocation

`GenerateUniqueGuid()` in `Guid.h` creates a UUID, checks the shared process registry, and reserves it atomically before returning. Empty candidates and collisions are retried. `Guid::generate()` forwards to this function; future GUID-using systems should use the same entry point. `GlobalGuidRegistry().hasUsed(id)` checks the shared registry.

Objects, item definitions, named object types, and individual item **stacks** all use this allocator. The readable catalog IDs (`wood_log`, etc.) remain separate from automatic GUIDs. `SceneWorld::createItemDefinition`, `createObjectType`, `placeObject`, and `addItem` assign fresh identities; uncommitted item drafts have empty GUIDs until Apply. The catalogs display full GUIDs, and the object inspector displays each stack GUID.

Quantity/name edits and object movement preserve identity. Adding a copy of an item creates a new stack with a different GUID. Duplicated or foreign item GUIDs in a contents update are rejected before changing the object. Removing items/objects or clearing cells does not release their identifiers for reuse.

Version-4 saves preserve all identities plus the world's used-GUID history. Loading reserves the history in the shared allocator and rejects identities duplicated across systems, missing history entries, or inconsistent ownership. Versions 1-3 remain readable; older item/type records receive GUIDs during migration. Save the migrated world to retain those newly assigned IDs. Reopening the same save restores the same logical identities; it does not create duplicates with new IDs. History is shared across systems in the running application and retained per saved world across restarts.

```cpp
const Guid id = GenerateUniqueGuid(); // Already collision-checked and reserved.
bool used = GlobalGuidRegistry().hasUsed(id);
const Guid stackId = grid.findObject(chest)->contents().at(0).guid;
```

## Grid navigation

```cpp
auto path = grid.aStar({10, 10}, {40, 30});
auto shortestSteps = grid.breadthFirstSearch({10, 10}, {40, 30});
bool visible = grid.hasLineOfSight({10, 10}, {40, 30});
```

Paths include start and goal. Empty paths indicate invalid/blocked endpoints or an unreachable goal. Both searches default to four-way movement; pass `true` as the third argument for diagonals. A* minimizes distance using cardinal cost 1 and diagonal cost sqrt(2), with Manhattan/octile heuristics. BFS minimizes step count, treating every allowed move as one step. Neither permits diagonals across blocked corners. Tile choices do not change movement cost.

LOS checks the segment between cell centers using a supercover traversal. **Walls, Entities, and closed Doors block vision**; open Doors, Containers, Gatherables, and missing Ground do not. Occluding endpoints and cells touched only at a corner block the ray. Invalid endpoints return false. The check is symmetric and does not allocate a world-sized buffer.

These are synchronous grid APIs; call them when a route is requested or obstacles change, not for every drawn cell. Searches can explore the whole grid if no route exists. No pathfinding controls are automatically bound to game input.

## Offline checks

```powershell
node scripts/test-jev.mjs --dry-run
node --test scripts/test-jev.test.mjs
cmake --build cmake-build-debug --target JevSimulation NodeLifecycleHost GridTests NavigationTests MapStorageTests ObjectTests CatalogTests GuidTests SceneWorldTests
ctest --test-dir cmake-build-debug --output-on-failure
```

These checks do not call Jev. The tests use fake HTTP responses and do not establish whether your live key or model access works. `JevSimulation --dry-run` is headless. For an offline graphics check, run `cmake-build-debug/JevSimulation.exe --window-smoke-test`: it creates a hidden window, renders 120 frames, and checks that rendering continues after Node finishes its dry run. Normal application runs never auto-close.

Build the `TilesetEditorTests` target and run `cmake-build-debug/TilesetEditorTests.exe` for hidden-window editor interaction tests with the real tilesheets. Pass an optional PNG output path to capture its rendered sample map. This test requires OpenGL and does not call Jev.

## Official references

- [Vercel evaluation HTTP API and response format](https://vercel.com/docs/ai-gateway/modalities/evaluation#http-api)
- [AI Gateway API keys](https://vercel.com/docs/ai-gateway/authentication-and-byok/api-keys)

The script uses the documented `POST https://ai-gateway.vercel.sh/v1/evaluate` endpoint with Bearer authentication. Jev returns typed decisions and probabilities rather than conversational prose.

## Flecs editor and game foundation

CMake pins and statically links Flecs **v4.1.6**. Normal launch opens the authoring editor; `JevSimulation --game` loads the same map and runs simulation without authoring controls. F1 only changes editor visibility.

The second menu row provides **Undo, Redo, Play, Pause/Resume, Step, Stop**. Ctrl+Z/Ctrl+Y work outside focused text fields. A complete paint/erase stroke, an inspector Apply, and a catalog creation each form one undo command. The latest 100 commands store changed cells/entities, preserving logical GUIDs through undo/redo. New edits discard redo. Successful loading clears history. The status bar marks unsaved changes, including undoing a saved catalog creation.

Play clones committed authoring data in memory, including unsaved changes. Inspector/form drafts stay in the authoring editor. Simulation runs at 60 ticks/second with at most five catch-up ticks per frame; excess time after long stalls is discarded. Pause runs no ticks, and Step runs exactly one. During Pause, the inspector can edit the runtime copy and use its separate undo history. Painting, catalog creation, and disk Save/Load are disabled during Play. Stop restores the authoring camera and selection and discards runtime content, while reserving any new runtime GUIDs. Play transitions never restart Node or send another Jev request.

Objects > Inspect > **Fields** displays centrally registered component fields and definition relationships. Click a stack's heading/GUID for its item components and ContainedBy relationship; **Properties** returns to Apply/Cancel editing. Scroll to see all fields. Identity, ownership, position, sprite, and component composition are read-only here.

`SceneWorld` is the runtime mutation boundary and owns Flecs, the dense terrain grid, lookup indexes, and GUID history. Objects use PersistentId, GridPosition, TileSprite and behavior components; every inventory row is an item entity with an exclusive ContainedBy relationship and ItemOrder. Definitions are prefabs with GUID inheritance explicitly disabled. Object queries return optional value snapshots, and UI code retains GUIDs rather than ECS handles. Grid only stores dense terrain and derived spatial/render references; empty terrain does not create ECS entities.

`WorldDocument` holds ECS-free snapshot/serialization records with validated import helpers. It is never a second live object registry. The existing version-4 codec and atomic replacement remain unchanged; versions 1–3 still migrate. Flecs runtime handles never appear in save files. Commands reconcile changed spatial references immediately between simulation iterations; the ordered CommandProcessing, Gameplay, and SpatialReconciliation phases provide the single-threaded simulation foundation.

`PlaySession.h` has no window-creation dependency. `SceneWorldTests` covers ownership, prefab independence, deltas, history limits, snapshot isolation, and controlled fixed-step times. The graphical test covers the transport and inspector at 900×600. `--window-smoke-test` also cycles Play/Pause/Step/Stop with the offline Node check; add `--game` to exercise the standalone game pipeline offline.


## Reusable resource pools

`Resource.h` defines `scene::Resource`, an ECS component containing independent named `ResourcePool` values. Built-in key constants are `Resource::Health`, `Resource::Stamina`, and `Resource::Mana`; custom keys such as `fuel`, `shield`, and `durability` work identically. Components need no GUID of their own and can be attached to any Flecs entity. Prefab instantiation copies resource state into each instance instead of sharing mutable values.

A pool stores current and maximum floating-point values. One-argument construction starts full; two arguments specify maximum and current. Both must be finite and nonnegative, and initial current cannot exceed maximum. Adjustments clamp to `[0, maximum]`; `trySpend` either pays the entire nonnegative amount or changes nothing. Lowering maximum clamps current; raising it does not refill. Pools expose `fraction`, `full`, `depleted`, `setCurrent`, `setMaximum`, `refill`, and `empty`. Zero maximum is supported, with fraction zero. No automatic regeneration, damage, death, or resource costs are implied.

Use the world command APIs for persistent game/editor instances:

```cpp
world.setResource(characterGuid, scene::Resource::Health, scene::ResourcePool(100));
world.setResource(characterGuid, scene::Resource::Mana, scene::ResourcePool(80, 30));
world.adjustResource(characterGuid, "health", -15); // Damage: clamped, returns actual change.
bool cast = world.trySpendResource(characterGuid, "mana", 10); // No overdraft.
auto health = world.resource(characterGuid, "health"); // Optional value snapshot.
world.removeResource(characterGuid, "mana");
```

These commands work on placed objects and contained item entities, participate in undo/redo, and preserve resources across moves and contents/property edits. Resource values appear under the inspector's **Fields** view; creation/editing is currently through the C++ APIs. Play gets independent resource pools. Maps with resource pools use **version 5** to persist owner GUIDs, names, current values, and maxima. Maps without pools continue writing version 4; versions 1–4 still load without pools. Loading rejects duplicate keys/owners, missing owners, invalid floats, and excessive collection sizes. Keys are case-sensitive, nonblank, and limited to 64 bytes, with at most 64 pools per entity.
