# Living entities

`Entity.h` provides the creature data foundation. New entities receive a shared
unique GUID, an extensible type key (default `creature`), and Health at 100/100.
Health uses `scene::Resource`, supports custom starting values, and cannot be
removed. Optional named resources can be added independently. Copies are value
snapshots with the same identity and independent resource state.

```cpp
#include "Entity.h"

Entity wolf("wolf", scene::ResourcePool(80));
wolf.adjustResource(scene::Resource::Health, -15);
float remainingHealth = wolf.health().current(); // 65
```

Type keys identify future component compositions without requiring inheritance
per species. World placement, Flecs ownership, chunk indexing, creature serialization, and
undo/redo are supported through SceneWorld. Species/Subspecies and Vocation
components are automatically attached; Human/Default/Unassigned are the defaults.
Editable catalogs assign sprite sheets, idle frames, allowed species per vocation,
and resource capacities per species. Use the Entities panel's Species / Vocation
Editors; definitions and assignments save with the map. Depleted health
does not automatically destroy an entity.

## Component data for Jev

`scene::Resource::fetchData() const` returns a JSON string snapshot containing all
named pools and their numeric `current` and `maximum` values:

```cpp
std::string data = wolf.resources().fetchData();
// Includes component rulesDescription and health current=65, maximum=80,
// rulesDescription="Remaining vitality. Zero means depleted; death is not automatic."
```

The component name identifies the payload when collecting data from multiple
components. Empty components output an empty `pools` object. Keys are sorted for
stable output, JSON special characters are escaped, and numbers are independent
of the machine's locale. Valid UTF-8 keys are preserved; malformed UTF-8 throws
`std::invalid_argument`. Fetching does not modify resources or send a Jev request.

`Resource::RulesDescription` defines shared pool rules. JSON includes it as a
`rulesDescription` string at component level and includes a concise description
on each pool. Health, stamina, and mana have centrally defined descriptions;
custom keys explicitly report an unspecified gameplay purpose. These constants
describe current behavior and should be updated when gameplay rules change.
They are code-defined metadata, so existing saves need no migration.

## Position

Every `Entity` owns a required `scene::Position`, defaulting to world `(0, 0)` and
cell `(0, 0)` with 8-unit cells and origin `(0, 0)`. Supply a third constructor
argument to set a starting position or different grid geometry.

```cpp
wolf.position().setWorldPosition({20.5f, 12}); // Cell (2, 1).
std::string location = wolf.position().fetchData();
wolf.position().setCellCoordinates({3, 2}); // World (24, 16), cell top-left.
```

Position JSON includes `component`, `rulesDescription`, `worldPosition` and
`cellCoordinates` (each with numeric `x` and `y`), `cellSize`, and `gridOrigin`.
Cell coordinates are derived using floor, including for negative positions.
Updates keep both coordinate representations synchronized and reject nonfinite
or unrepresentable coordinates without changing the previous state. Cell updates
also reject corners that cannot map back to the requested cell at float precision.
World bounds and occupancy are checked by the world, not this data component.
Copies retain independent position state. Fetching does not move the entity or
send a Jev request; world movement commands maintain occupancy and chunk membership.
See [AI_DECISIONS.md](AI_DECISIONS.md) for runtime commands and the decision loop.

## Range

`scene::Range` (`Range.h`) is a reusable nonnegative integer cell-distance budget.
Cardinal steps cost 1 and diagonal steps cost 2, so a radius forms a diamond:
`abs(dx) + abs(dy) <= radius`. The center and boundary are included; radius zero
includes only the center. `distance` and `contains` use cell coordinates.

```cpp
scene::Range sight(5);
auto center = wolf.position().cellCoordinates();
bool nearby = sight.contains(center, {4, 2});
auto cells = sight.cells(center, {0, 0, world.width(), world.height()});
std::string rangeData = sight.fetchData(center);
```

`cells` returns coordinates in row-major order clipped to the supplied half-open
cell bounds. `fetchData(center)` exports `component`, concise `rulesDescription`,
`radius`, `center`, `cardinalCost`, and `diagonalCost`. The payload describes the
area without enumerating its contents. Passing the current Position cell avoids
a stale center after movement. Independent Range instances can later support
sight, hearing, weapons, and movement with different radii; none is automatically
assigned to Entity yet. Range alone does not evaluate obstacles, line of sight,
sound, pathfinding, or terrain contents, and it does not change navigation costs.

Human species (ID 0) receive a random first and last name when placed/spawned or
when migrated from an older map. Names are separate from species/type labels and
appear in the paused tooltip and inspection data. Full combinations are unique
within a world, including retired entities: deletion, death and undo never release
a reservation. Save format v10 persists reservations and Play copies retain them.
Nonhuman species have no human name. Changing species away and back restores the
original name. If all 1,024 base combinations are used, compound surname variants
keep generation bounded and unique.
