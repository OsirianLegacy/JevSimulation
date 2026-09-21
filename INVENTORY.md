# Entity inventory

Every creature has the `scene::Inventory` ECS ownership component, shared with
containers and gatherables. The component is defined in `Inventory.h`. Stacks
are separate entities with `PersistentId`, `ItemStack`, and `ItemOrder`, linked
to their owner through the exclusive `ContainedBy` relationship. This keeps one
live source of inventory state and lets stacks retain their own Resource pools.

Use SceneWorld commands to change persistent inventories:

```cpp
world.addItem(creatureId, Item{"apple", "Apple", 3});
auto items = world.inventory(creatureId); // optional value snapshot
world.updateItem(creatureId, 0, Item{"apple", "Apple", 2});
world.removeItem(creatureId, 0);
world.updateInventory(creatureId, {}); // clear all stacks
```

`findCreature(id)->inventory()` also returns the snapshot's read-only item list.
`inventory(id)` returns nullopt for missing owners and doors; creatures start
with an empty inventory. The same world commands work on containers and
gatherables. Each added stack gets a fresh GUID, even when adding a copied Item.
Updates preserve the stack GUID; replacement lists may retain/reorder existing
stacks or add new ones with empty GUIDs, but cannot import another owner's GUID.
Removed stack identities remain reserved.

Unit inventories permit up to six stacks (one per slot); containers and world objects retain their 1024-stack capacity. Stacks have positive uint32 quantities and validated
ID/name text of 1-255 bytes. Gatherable pickup merges matching items up to ten units
per slot, then uses additional slots for overflow. Matching requires the same item
ID, display name, and resource state. A pickup must fit completely; otherwise it
stays on the ground. Explicit inventory editing commands retain their stack layout. Item definitions
are optional, as with existing object contents. Weight, equipment slots, transfers,
consumption, and an inventory editing UI are not implemented in this foundation.

Inventory commands support undo/redo, cancellation, and isolated Play copies.
Creature deletion removes owned stacks; undo restores their IDs and resources.
Component inspection reports the stack count and stack ownership; creature
inspection JSON includes item identities, names, quantities, and inventory rules.

Map version 8 appends each creature's ordered inventory records, including stack
GUIDs. Versions 1-7 still load; older creatures receive empty inventories. Loading
rejects invalid stacks, duplicate identities across owners/systems, missing GUID
history, excessive counts, and truncated records.

Paused unit tooltips display six slots beneath the needs bars, using the existing
`Assets/UI/Frames.png` artwork. Occupied slots show item labels and quantities.
The six-slot limit applies to player and AI units, pickup, API edits, Play copies,
and save validation. Over-capacity unit saves are rejected instead of discarding items.
