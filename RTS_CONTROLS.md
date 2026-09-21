# Player selection and orders

RTS controls operate in Play and standalone `--game` mode. Authoring controls
remain available outside Play.

- Left-click a living player-controlled unit to select it.
- Hold and drag the left button to select all player unit centers inside the box.
  Dragging works in either direction. A five-pixel threshold distinguishes a box
  from a click.
- Shift-click adds an unselected unit or removes a selected one. Shift-drag adds
  units without toggling existing members. Clicking empty terrain clears selection;
  Shift-clicking empty terrain keeps it.
- Right-click terrain to move. Groups receive separate destinations around the
  clicked cell, spaced two cells apart. Each accepted order uses the existing timed
  four-way pathfinding action. A single unit targets the exact clicked cell.
- Right-click a Harvestable to issue a harvesting action to each eligible selected
  unit. The action handles approach, tools, work, yields, and depletion.
- Right-click a loose Gatherable to approach a cardinal cell for automatic pickup.
- Right-click another living entity to attack it. Units approach a cardinally
  adjacent cell and deal 10 damage each second until the target dies. Explicit
  player orders can attack neutral or friendly entities as well as hostiles.
- X stops the selected units. New accepted orders replace their previous actions.
- WASD pans the camera; the mouse wheel zooms. Arrow keys no longer directly move
  a player entity.

Green outlines show selection, lines show active move destinations, and a brief
green/red click marker indicates whether any order was accepted. The HUD reports
the selected count and how many orders were accepted. Units with rejected orders keep
their previous action. Unreachable destinations never teleport units.

Selection and orders work while paused; actions advance only when simulation
resumes or steps. NPCs and depleted units cannot be selected. Removed/dead units
are pruned from selection, and world/session changes clear it. Editor panels and
focused text fields block world commands; focus loss cancels an unfinished drag.

`PlayerController` keeps GUID-based selection outside saved world data and takes
explicit input for testing. Group destinations are distinct, but this first version
uses the existing collision/reroute behavior rather than crowd steering or a full
formation reservation system. Crowded chokepoints can still cause route failures;
blocked units can be reordered after the route clears. There is no Shift order
queue yet.
