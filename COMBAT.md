# Combat and disposition

In **Entities > Species / Vocation Editors > Species**, click **Default** to
cycle Neutral, Friendly, and Hostile, then **Apply + Save map**. For example,
set deer/rabbits to Neutral, wolves to Hostile, and allied species to Friendly.
New species and legacy definitions default to Neutral.

Each creature copies its species default when spawned or reassigned to a different
species. Editing a species default affects future spawns; existing creatures keep
their current disposition. `SceneWorld::setCreatureDisposition` changes an individual
with undo/redo support. Paused tooltips and inspection JSON display the state.
Map version 11 persists individual dispositions. Earlier maps load using species
defaults, falling back to Neutral for definitions without a default. Play copies
preserve dispositions independently of the authoring world.

Player, local AI, and Jev use the same `attack` action with parameters
`{"target":"<entity GUID>"}`. Right-click another living entity with player units
selected to issue it. X cancels; accepted move/harvest orders replace it.
The attacker follows a four-way route to an adjacent cell, then deals 10 Health
damage per one-second work interval. Movement interrupts the attack interval.
Moving targets are followed; blocked routes fail after bounded retries. Pause
freezes attacks and Step advances them. Dead/missing actors cannot attack,
self-targeting is rejected, and dead targets cannot take further hits. Configured
species enter the existing carcass lifecycle after death.

Disposition is an intrinsic state, independent of Player/AI control:

- Hostile local AI and Jev candidates target non-hostile entities.
- Friendly local AI and Jev candidates target hostile entities.
- Neutral entities do not initiate combat autonomously.
- Two player-controlled units are not automatic enemies. Explicit player orders
  can attack any other living entity, including neutral wildlife or friendlies.

Local fallback prioritizes an eligible attack over harvesting or wandering.
Jev may choose among the supplied attack, harvest, movement, and wait candidates.
Autonomous attacks stop if disposition changes make the target cease to be an
enemy or make the attacker Neutral. This is a simple disposition system; it does
not model factions, species-specific rivalries, or automatic retaliation.

Decision context includes self disposition and `nearbyEntities` with GUID,
species, disposition, relative `enemy` flag, control, cell, and current/maximum
health. Perception uses the configured Manhattan radius and line of sight;
walls, closed doors, and intervening creatures block visibility. Creature endpoints
are allowed. Entries are nearest first, capped at 16 and reduced if necessary to
meet the request byte limit. `nearbyEntityCount` reports the full visible count.
Attack candidates carry target disposition and health even when the nearby list
is shortened. Route checks and candidate counts remain bounded.
