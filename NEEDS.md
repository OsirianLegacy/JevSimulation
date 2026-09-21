# Basic creature needs

Needs use the existing Resource component: `Resource::Hunger` (`hunger`),
`Resource::Thirst` (`thirst`), and `Resource::Sleep` (`sleep`). Higher values mean
better satisfied: food, water, and rest reserves. New species default to 100 of
each, and creatures spawned from those species start full. Saved species retain
their configured pools; add these keys in the Species resource editor to enable
them on future spawns, or use `setResource` for an existing creature.

During Play/game simulation, present creature need pools drain by a fraction of
their maximum each second: Hunger lasts 600 seconds, Thirst 300, and Sleep 900.
These initial tuning constants live in `Resource.h`. Pause freezes needs; Step
advances one tick. Missing/zero-capacity needs are disabled. Objects and items do
not drain. Values clamp at zero without causing damage or death.

Restore reserves through the existing world commands:

```cpp
world.adjustResource(id, scene::Resource::Hunger, 25);
world.adjustResource(id, scene::Resource::Thirst, 50);
world.adjustResource(id, scene::Resource::Sleep, 100);
```

Food/drink consumption and sleep actions are not implemented yet. Need values
use existing save/load and play-copy support, appear in inspection JSON with their
rules, and display alongside Health in the paused creature tooltip. Generic
Resource instances and standalone Entity values do not advance automatically;
SceneWorld runs `Resource::advanceNeeds` on creature components every fixed tick.
