# Harvesting

Harvestable is an optional component on Gatherable world objects. It references
a reusable definition in the world catalog and uses the Resource component's
`harvestUnits` pool for remaining whole harvests. Loose Gatherables with contents are automatically collected on cardinal adjacency.
Harvestable sources retain their stored contents separately from generated yields.

The supplied definitions are tree (wood and branches), ore (ore and stone), rock
(stone), and carcass (hide, meat and bones). These are configurable starting values.
Leather requires a future crafting step. Each work interval spends one unit and reduces Health proportionally. At 75%, 50%,
25%, and 0% health, each recipe yield drops a stack with a random quantity from 1
to its configured maximum. Multiple crossed thresholds each produce a burst.
Stacks scatter to unique, unoccupied walkable terrain within four cells of the node.
If a complete burst cannot fit, the work interval consumes nothing (`no_drop_space`).

## Editor workflow

1. Choose artwork in Tile Painting. Open **Harvestables**, select a definition,
   then click **Place in level**. Click or drag on the map to place sources.
2. Alternatively, use **Tile Painting > Objects > Paint > Harvest** to cycle the
   brush definition. Choose None for ordinary objects. Existing Gatherables can
   still be assigned using **Objects > Inspect > Harvest > Apply**.
3. Open **Object Types > Harvest definitions...** to browse, copy, and configure
   recipes. A Harvestables top tab is also available on wider windows.
4. Configure action, work duration, harvest count, yields, tool ID, depletion name,
   removal policy, and optional regeneration seconds. Apply + Save persists it.
5. Optionally select stump/rubble artwork in Tile Painting, return to the definition,
   and choose **Set depleted tile from brush**. Without this, the original artwork
   remains. Regeneration restores the original node tile.

Assigned definitions are immutable: use **Copy to new**, choose a unique ID, save,
then reassign the node. Assignment starts the node full; unchanged inspector Apply
does not refill it. Fields inspection shows units, state, yields, tool, work time,
and remaining regeneration time. Undo/redo includes assignment and depletion.

## Playing

Place a Player entity, start Play, and left-click it to select it. Right-click a
harvestable source to order harvesting. The actor walks to a reachable cardinally
adjacent cell and repeats work until depletion. X cancels the current action;
new move orders replace it too. The HUD shows selection and order feedback.
AI receives eligible nearby harvest candidates with yields, tools and remaining
units, using the same action implementation as players. Candidate route searches
are bounded and the existing decision payload limit remains enforced.

Pause freezes both work and regeneration; Step advances simulation. Cancellation
loses incomplete work. Completion rechecks health, target, adjacency, recipe,
tool presence, units, free drop cells, and GUID capacity. Failures create
no output and spend no units. Two actors competing for the last unit cannot both
receive it. Tool requirements currently check inventory presence only; tools are
not consumed and durability is not reduced.

Removal on depletion only removes nodes whose stored inventory is empty. Nodes
with separate loot remain depleted so it cannot be destroyed accidentally.
Regeneration is optional, deterministic, and uses simulation seconds; removal
and regeneration cannot both be enabled. No tool durability,
equipment slots, crafting, or dedicated looting action are implemented.

## Animals

In the Harvest definitions editor, select the carcass recipe, cycle the species,
and choose **Assign + use current tile brush**. This stores both the species'
carcass definition and carcass artwork. Disable carcass preserves the legacy
health-only lifecycle. Species receive no carcass configuration automatically.

At zero Health, creatures cannot act. On the next world tick, configured species
become Gatherable carcasses. Their carried stacks retain GUIDs, quantities and
item Resource components; butcher yields remain a separate recipe. If an object
already occupies the death cell, the first walkable adjacent cell is used. If none
is available, conversion waits and the depleted creature remains inert. Carcass
creation is one undoable operation. Existing action states cancel when the actor
disappears; conversion never produces butcher yields on its own.

## APIs and persistence

```cpp
world.setHarvestable(treeId, "tree");
auto check = world.canHarvest(workerId, treeId);
auto path = world.harvestPath(workerId, treeId);
decisions.assign(workerId, "harvest", {{"target", treeId.toString()}}, "player");
```

`completeHarvest` is the simulation commit API after timed work; direct callers
are responsible for elapsed work. `canHarvest(..., false)` checks prerequisites
without requiring adjacency, for planning. `removeHarvestable` removes the binding
and units and restores the source artwork, preserving unrelated Resource pools.

Map version 9 persists definitions, bindings, original artwork and regeneration
countdowns. Versions 1-8 still load without harvest bindings. Species carcass and
depleted sprite references remap by tilesheet name across library reordering.
Snapshots, Play copies, save/load, and undo/redo preserve stack ownership without
duplicating items. Active work is transient action state and is not saved.

## Automatic pickup

On each simulation tick, a loose Gatherable checks its four cardinal neighboring
cells for living creatures with enough inventory capacity for its contents.
Matching item IDs, names, and resource states fill existing stacks up to ten units
before using another slot. Larger drops split into stacks of at most ten.
Choose randomly among eligible players if any exist, otherwise among eligible AI.
Full or dead players do not prevent AI pickup. Diagonals do not count. If no one
can fit the contents, the Gatherable stays on the ground. Right-clicking a loose
Gatherable orders selected units to an adjacent cell.

Pickup retains destination stack identities, transfers unmerged source identities,
and gives split overflow fresh identities. Fully merged source identities are retired.
Item resources are preserved. Pickup removes the
empty world object, all in one undoable operation. It never generates new yields.
Pause freezes pickup; Step processes it. Health, loose stacks, and milestone
progress survive save/load and Play copies. Drops use a gold pickup marker;
damaged sources show a green health bar.
