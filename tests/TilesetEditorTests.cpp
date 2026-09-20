#include "TilesetEditor.h"
#include "WorldCamera.h"
#include "NodeConfig.h"
#include "PlaySession.h"
#include <iostream>
#include <stdexcept>
#include <string>

void check(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }

int main(int argc, char* argv[]) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1100, 700, "Tileset editor interaction tests");
    if (!IsWindowReady()) return 1;
    int result = 0;
    try {
        TilesetLibrary library(JevTilesetDirectory);
        check(library.count() >= 3 && library.get(0)->name == "Overworld.png", "Default tileset and asset discovery.");
        check(library.get(0)->columns == 24 && library.get(0)->rows == 93, "8x8 Overworld atlas dimensions.");
        int walls = -1, structures = -1;
        for (int i = 0; i < library.count(); ++i) {
            if (library.get(i)->name == "Walls.png") walls = i;
            if (library.get(i)->name == "Structures.png") structures = i;
        }
        check(walls >= 0 && structures >= 0, "Required tilesheets.");
        check(!library.valid({0, 24, 0}) && !library.valid({-1, 0, 0}), "Atlas bounds validation.");
        SceneWorld grid;
        WorldCamera camera(grid.worldBounds());
        camera.setZoom(3);
        camera.resize(1100, 700);
        TilesetEditor editor(library);
        const auto update = [&](EditorInput input) { editor.update(grid, camera.camera(), input, 700); };
        const auto release = [&] { update({}); };
        const auto click = [&](Vector2 position, bool right = false) {
            EditorInput input;
            input.mouse = position;
            input.leftPressed = input.leftDown = !right;
            input.rightPressed = input.rightDown = right;
            update(input);
        };
        const auto chooseSheet = [&](int id) {
            click({100, 62}); release();
            click({100, static_cast<float>(94 + id * 32)}); release();
        };
        const CellPosition a{506, 500}, b{512, 500};
        const auto screenA = GetWorldToScreen2D(grid.cellCenter(a), camera.camera());
        const auto screenB = GetWorldToScreen2D(grid.cellCenter(b), camera.camera());
        click(screenA); release();
        check(!grid.isWalkable(a), "Hidden editor cannot paint.");
        EditorInput toggle; toggle.toggle = true; update(toggle); release();
        check(editor.visible(), "F1 opens panel.");
        click({56, 238});
        check(editor.selected() == TileRef{0, 2, 1}, "Palette selects exact 8x8 tile.");
        EditorInput drag; drag.mouse = screenA; drag.leftDown = true; update(drag);
        check(!grid.isWalkable(a), "Palette drag cannot paint through onto map.");
        release();
        click(screenA);
        drag.mouse = screenB; update(drag); release();
        for (int x = a.x; x <= b.x; ++x) {
            check(grid.isWalkable({x, a.y}), "Paint stroke interpolation fills skipped cells.");
            check(grid.at({x, a.y}).tile(GridLayer::Ground) == TileRef{0, 2, 1}, "Painted tile identity.");
        }
        chooseSheet(walls);
        click({240, 122}); release();
        check(editor.layer() == GridLayer::Walls, "Wall layer selector.");
        click(screenA); release();
        check(!grid.isWalkable(a) && grid.blocksSight(a), "Wall painting updates movement and vision.");
        check(grid.at(a).tile(GridLayer::Ground) == TileRef{0, 2, 1}, "Wall painting preserves ground/tileset.");
        click(screenA, true); release();
        check(grid.isWalkable(a) && !grid.blocksSight(a), "Wall eraser restores ground movement.");
        click({80, 122}); release();
        click(screenA, true); release();
        check(!grid.isWalkable(a) && !grid.blocksSight(a), "Erasing ground does not block vision.");
        click({240, 155}); release();
        check(editor.layer() == GridLayer::Ground, "Reserved entity tab cannot activate painting.");

        chooseSheet(structures);
        // Scroll to both far edges using the draggable tracks, then select the last tile.
        click({327, 553}); release();
        click({339, 541}); release();
        click({327, 541}); release();
        check(editor.selected() == TileRef{structures, 59, 108}, "Scrollbars expose entire oversized tilesheet.");
        click(screenA); release();
        check(grid.at(a).tile(GridLayer::Ground) == editor.selected(), "Painting stores each cell's own tileset.");
        chooseSheet(0);
        check(grid.at(a).tile(GridLayer::Ground).tileset == structures, "Dropdown changes do not replace existing tiles.");
        click({100, 62}); release();
        const CellPosition untouched{510, 501};
        const auto untouchedScreen = GetWorldToScreen2D(grid.cellCenter(untouched), camera.camera());
        click(untouchedScreen);
        drag.mouse = untouchedScreen; update(drag); release();
        check(!grid.isWalkable(untouched), "Dismissed dropdown cannot paint through on a held click.");
        update(toggle); release();
        check(!editor.visible(), "F1 closes panel.");
        EditorInput saveShortcut; saveShortcut.save = true; update(saveShortcut);
        check(editor.takeMapAction() == MapAction::Save && editor.takeMapAction() == MapAction::None, "Save shortcut works when panel is closed, exactly once.");
        EditorInput loadShortcut; loadShortcut.load = true; update(loadShortcut);
        check(editor.takeMapAction() == MapAction::Load, "Load shortcut works when panel is closed.");
        click(untouchedScreen); release();
        check(!grid.isWalkable(untouched), "Closed panel disables painting.");
        update(toggle); release();
        click({80, 580}); release();
        check(editor.takeMapAction() == MapAction::Save, "Editor Save button.");
        click({240, 580}); release();
        check(editor.takeMapAction() == MapAction::Load, "Editor Load button.");

        // Objects preserve existing instances and expose drafts through Inspect mode.
        click({80,155}); release();
        check(editor.layer() == GridLayer::Objects, "Objects tab is enabled.");
        click(screenA); release();
        const auto chest = grid.at(a).objectId;
        check(!chest.empty() && grid.findObject(chest)->isContainer(), "Container placement.");
        click({48,228}); release(); // Door brush
        click(screenA); release();
        check(grid.at(a).objectId == chest && grid.findObject(chest)->isContainer(), "Occupied cell is preserved.");
        click(screenB); release();
        const auto door = grid.at(b).objectId;
        check(grid.findObject(door)->type() == ObjectType::Door && grid.blocksSight(b), "Door placement.");
        click({240,190}); release(); // Inspect
        click(screenA); release();
        click({100,360}); release(); // Add stack
        const auto typeText = [&](Vector2 position, const std::string& text, bool erase = false) {
            click(position); release();
            check(editor.capturesKeyboard(), "Text input captures camera keys.");
            EditorInput input; input.mouse = position; input.text = text; input.backspace = erase; update(input); release();
        };
        typeText({100,388}, "wood");
        typeText({100,418}, "Wood");
        typeText({150,450}, "42", true);
        check(grid.findObject(chest)->contents().empty(), "Draft does not mutate object.");
        click({80,544}); release();
        const auto stackGuid=grid.findObject(chest)->contents().at(0).guid;
        check(!stackGuid.empty() && grid.findObject(chest)->contents() == std::vector<Item>{{"wood","Wood",42,stackGuid}}, "Apply item stack assigns GUID.");
        click({100,304}); release(); // Open draft
        click({240,544}); release(); // Cancel
        check(!grid.findObject(chest)->isOpen(), "Cancel restores open state.");
        check(grid.findObject(chest)->contents()[0].guid==stackGuid,"Inspector preserves item identity on edits.");
        typeText({150,450}, "", true); // 42 -> 4
        typeText({150,450}, "0", true); // 4 -> 0
        click({80,544}); release();
        check(grid.findObject(chest)->contents()[0].quantity == 42, "Invalid draft cannot replace contents.");
        click({240,544}); release();
        click({300,360}); release(); // Remove row draft
        click({80,544}); release();
        check(grid.findObject(chest)->contents().empty(), "Remove stack and apply.");
        click(screenB); release();
        click({100,304}); release(); click({80,544}); release();
        check(grid.findObject(door)->isOpen() && grid.isWalkable(b) && !grid.blocksSight(b), "Inspector opens door.");
        click(screenB, true); release();
        check(!grid.findObject(door), "Inspect eraser removes instance.");
        editor.clearSelection();
        check(!editor.capturesKeyboard(), "Map-load selection reset releases keyboard.");

        click({80,122}); release(); chooseSheet(0);
        click({72,254});
        EditorInput rectangleDrag; rectangleDrag.mouse={40,238}; rectangleDrag.leftDown=true; update(rectangleDrag); release();
        check(editor.selected()==TileRef{0,1,1} && editor.selectionSize()==CellPosition{3,2},"Reverse atlas drag selects a 3x2 rectangle.");
        click(screenA); release();
        for(int y=0;y<2;++y) for(int x=0;x<3;++x)
            check(grid.at({a.x+x,a.y+y}).tile(GridLayer::Ground)==TileRef{0,1+x,1+y},"Rectangle stamp preserves source arrangement.");
        click(screenA);
        EditorInput patternDrag;patternDrag.mouse=screenB;patternDrag.leftDown=true;update(patternDrag);release();
        for(int x=0;x<9;++x) check(grid.at({a.x+x,a.y}).tile(GridLayer::Ground)==TileRef{0,1+x%3,1},"Drag repeats aligned tile stamps without smearing.");
        click(screenA,true); release();
        for(int y=0;y<2;++y) for(int x=0;x<3;++x)
            check(!grid.at({a.x+x,a.y+y}).tile(GridLayer::Ground).present(),"Eraser uses selected rectangle footprint.");
        Camera2D edgeCamera{};edgeCamera.zoom=3;edgeCamera.target=grid.cellCenter({999,999});edgeCamera.offset={800,500};
        EditorInput edgeClick;edgeClick.mouse={800,500};edgeClick.leftPressed=edgeClick.leftDown=true;
        editor.update(grid,edgeCamera,edgeClick,700);release();
        check(grid.at({999,999}).tile(GridLayer::Ground)==TileRef{0,1,1},"Stamp clips safely at map edge.");

        click({600,22});release();
        check(editor.mode()==EditorMode::Items && editor.capturesMouse(screenA) && editor.capturesKeyboard(),"Item mode captures world and camera input.");
        typeText({100,140},"wood_log");typeText({100,210},"Wood log");
        click({100,320});release();
        check(grid.itemDefinitions().at("wood_log").displayName=="Wood log" && editor.takeMapAction()==MapAction::Save,"Create item requests persistent save.");
        click({100,320});release();
        check(grid.itemDefinitions().size()==1 && editor.takeMapAction()==MapAction::None,"Duplicate definition rejected without save.");
        click({790,22});release();
        check(editor.mode()==EditorMode::ObjectTypes,"Top menu switches to object types.");
        typeText({100,140},"oak_tree");typeText({100,210},"Oak Tree");
        click({290,270});release();click({100,320});release();
        check(grid.objectTypes().at("oak_tree").behavior==ObjectType::Gatherable && editor.takeMapAction()==MapAction::Save,"Create named Gatherable and request save.");
        click({440,22});release();click({80,155});release();click({80,190});release();
        click({100,260});release();click({100,288});release(); // Custom type dropdown.
        const CellPosition treeCell{516,504};const auto treeScreen=GetWorldToScreen2D(grid.cellCenter(treeCell),camera.camera());
        click(treeScreen);release();
        for(int y=0;y<2;++y) for(int x=0;x<3;++x)
            check(grid.objectAt({treeCell.x+x,treeCell.y+y})->typeDefinitionId()=="oak_tree","Multi-cell object brush creates named instances.");
        click({240,190});release();click(treeScreen);release();
        click({100,334});release(); // Choose catalog item for new stack.
        click({100,360});release();click({80,544});release();
        const auto treeItemGuid=grid.objectAt(treeCell)->contents().at(0).guid;
        check(!treeItemGuid.empty() && grid.objectAt(treeCell)->contents()==std::vector<Item>{{"wood_log","Wood log",1,treeItemGuid}},"Saved item definition populates object contents with a new GUID.");
        // Reset to a single tile before the remaining rendering checks.
        click({80,122});release();chooseSheet(0);click({24,222});release();
        click({80,155});release();click({240,190});release();

        // Render a sample map with real assets for visual verification.
        grid.clear();
        for (int y = 493; y < 511; ++y) for (int x = 497; x < 519; ++x)
            grid.paint({x, y}, GridLayer::Ground, {0, 0, 0});
        for (int x = 500; x < 516; ++x) if (x != 508)
            grid.paint({x, 498}, GridLayer::Walls, {walls, 1, 1});
        for (int y = 499; y < 507; ++y) grid.paint({500, y}, GridLayer::Walls, {walls, 1, 1});
        const auto render = [&](int height) {
            BeginDrawing();
            ClearBackground(Color{22, 26, 34, 255});
            BeginMode2D(camera.camera());
            grid.draw(camera.visibleBounds(), &library);
            editor.drawBrush(grid,camera.camera(),{700,400});
            EndMode2D();
            editor.draw(height);
            if(editor.mode()==EditorMode::Tiles) {
                DrawText("8 x 8 CELL PAINTING", 398, 90, 24, RAYWHITE);
                DrawText("Ground enables movement. Walls block movement and LOS.", 398, 122, 16, SKYBLUE);
            }
            EndDrawing();
        };
        for (int frame = 0; frame < 3; ++frame) render(700);
        if (argc > 1) {
            Image screenshot = LoadImageFromScreen();
            const bool saved = ExportImage(screenshot, argv[1]);
            UnloadImage(screenshot);
            check(saved, "Editor screenshot export failed.");
        }
        click({100, 62}); release(); render(700); // Dropdown rendering also exercised.
        click({100,62}); release();
        SetWindowSize(900, 600);
        camera.resize(900, 600);
        const auto sample = grid.placeObject({506,500}, {structures,0,0}, ObjectType::Container);
        grid.addItem(sample, {"wood", "Wood", 42});
        grid.addItem(sample, {"iron_ore", "Iron ore", 12});
        EditorInput inspectClick;
        inspectClick.mouse = GetWorldToScreen2D(grid.cellCenter({506,500}), camera.camera());
        inspectClick.leftPressed = inspectClick.leftDown = true;
        editor.update(grid, camera.camera(), inspectClick, 600);
        editor.update(grid, camera.camera(), {}, 600);
        for (int frame=0;frame<3;++frame) render(600);
        if (argc > 1) {
            Image screenshot = LoadImageFromScreen();
            check(ExportImage(screenshot, (std::string(argv[1])+".objects.png").c_str()), "Object inspector screenshot export.");
            UnloadImage(screenshot);
        }
        EditorInput scroll; scroll.mouse = {200,350}; scroll.wheel.y = -20;
        editor.update(grid,camera.camera(),scroll,600);
        EditorInput focus; focus.mouse={150,366}; focus.leftPressed=focus.leftDown=true;
        editor.update(grid,camera.camera(),focus,600);
        check(editor.capturesKeyboard(),"Scrolled item row can receive text focus at minimum size.");
        EditorInput edit; edit.mouse=focus.mouse; edit.backspace=true;
        editor.update(grid,camera.camera(),edit,600); // 12 -> 1
        edit.text="99";
        editor.update(grid,camera.camera(),edit,600); // 1 -> 99
        EditorInput apply; apply.mouse={80,444}; apply.leftPressed=apply.leftDown=true;
        editor.update(grid,camera.camera(),apply,600);
        check(grid.findObject(sample)->contents()[1].quantity==99,"Scroll exposes and edits second stack.");
        for (int frame=0;frame<3;++frame) render(600);
        if (argc > 1) {
            Image screenshot=LoadImageFromScreen();
            check(ExportImage(screenshot,(std::string(argv[1])+".scrolled.png").c_str()),"Scrolled inspector screenshot.");
            UnloadImage(screenshot);
        }
        const auto captureMode=[&](const char* suffix) {
            for(int frame=0;frame<3;++frame) render(600);
            if(argc>1) {
                Image screenshot=LoadImageFromScreen();
                check(ExportImage(screenshot,(std::string(argv[1])+suffix).c_str()),"Editor mode screenshot.");
                UnloadImage(screenshot);
            }
        };
        click({600,22});release();
        typeText({100,140},"iron_ore");typeText({100,210},"Iron Ore");
        captureMode(".items.png");
        click({790,22});release();
        typeText({100,140},"wooden_chest");typeText({100,210},"Wooden Chest");
        captureMode(".types.png");
        click({440,22});release();click({80,122});release();chooseSheet(0);
        click({24,222});EditorInput palette;palette.mouse={56,238};palette.leftDown=true;update(palette);release();
        captureMode(".selection.png");

        // ECS authoring and transport at the minimum supported window size.
        SceneWorld authored(100,100);WorldCamera ecsCamera(authored.worldBounds());ecsCamera.setZoom(3);ecsCamera.resize(900,600);
        const auto doorId=authored.placeObject({55,50},{0,0,0},ObjectType::Door);
        authored.setResource(doorId,scene::Resource::Health,scene::ResourcePool(100,70));
        authored.setHistoryEnabled(true);TilesetEditor ecsEditor(library,true);
        const auto ecsUpdate=[&](EditorInput input){ecsEditor.update(authored,ecsCamera.camera(),input,600);};
        const auto ecsClick=[&](Vector2 point){EditorInput i;i.mouse=point;i.leftDown=i.leftPressed=true;ecsUpdate(i);ecsUpdate({});};
        const auto start=GetWorldToScreen2D(authored.cellCenter({60,55}),ecsCamera.camera());
        const auto end=GetWorldToScreen2D(authored.cellCenter({63,55}),ecsCamera.camera());
        EditorInput stroke;stroke.mouse=start;stroke.leftPressed=stroke.leftDown=true;ecsUpdate(stroke);
        stroke.mouse=end;stroke.leftPressed=false;ecsUpdate(stroke);ecsUpdate({});
        check(authored.isWalkable({60,55}) && authored.isWalkable({63,55}),"Authoring stroke spans cells");
        ecsClick({402,60});check(!authored.isWalkable({60,55}) && !authored.isWalkable({63,55}) && !authored.canUndo(),"Undo button groups entire stroke");
        EditorInput redo;redo.redo=true;ecsUpdate(redo);check(authored.isWalkable({63,55}),"Ctrl+Y redoes stroke");
        ecsClick({80,155});ecsClick({240,190});
        ecsClick(GetWorldToScreen2D(authored.cellCenter({55,50}),ecsCamera.camera()));
        ecsClick({100,304}); // Leave open state as an uncommitted draft.
        ecsClick({570,60});check(ecsEditor.takeTransportAction()==TransportAction::Play,"Play button event");
        PlaySession session;session.start(authored);check(!session.world().findObject(doorId)->isOpen(),"Play excludes pending inspector drafts");
        TilesetEditor runtime(library,true);runtime.setPlayState(true,false);
        const auto runtimeUpdate=[&](EditorInput input){runtime.update(session.world(),ecsCamera.camera(),input,600);};
        const auto runtimeClick=[&](Vector2 p){EditorInput i;i.mouse=p;i.leftDown=i.leftPressed=true;runtimeUpdate(i);runtimeUpdate({});};
        runtimeClick(GetWorldToScreen2D(authored.cellCenter({55,50}),ecsCamera.camera()));
        runtimeClick({100,304});runtimeClick({80,444});check(!session.world().findObject(doorId)->isOpen(),"Running inspector is read-only");
        EditorInput disk;disk.save=disk.load=true;runtimeUpdate(disk);check(runtime.takeMapAction()==MapAction::None,"Disk shortcuts disabled during Play");
        runtimeClick({600,22});check(runtime.mode()==EditorMode::Tiles,"Play disables catalog modes");
        runtimeClick({652,60});check(runtime.takeTransportAction()==TransportAction::Pause,"Pause control event");session.pause(true);runtime.setPlayState(true,true);
        runtimeClick({100,304});runtimeClick({80,444});check(session.world().findObject(doorId)->isOpen() && !authored.findObject(doorId)->isOpen(),"Paused Apply edits only runtime components");
        runtimeClick({402,60});check(!session.world().findObject(doorId)->isOpen(),"Runtime undo is isolated");
        runtimeClick({740,60});check(runtime.takeTransportAction()==TransportAction::Step,"Step button event");session.step();check(session.world().ticks()==1,"Step action advances one tick");
        runtimeClick({290,226}); // Curated component metadata view.
        for(int i=0;i<3;++i){BeginDrawing();ClearBackground(BLACK);BeginMode2D(ecsCamera.camera());session.world().draw(ecsCamera.visibleBounds(),&library);EndMode2D();runtime.draw(600);EndDrawing();}
        if(argc>1){auto shot=LoadImageFromScreen();check(ExportImage(shot,(std::string(argv[1])+".ecs-play.png").c_str()),"Play inspector capture");UnloadImage(shot);}
        runtimeClick({830,60});check(runtime.takeTransportAction()==TransportAction::Stop,"Stop control event");session.stop(authored);
        check(!authored.findObject(doorId)->isOpen() && authored.canUndo(),"Stop preserves authoring state and history");
        ecsClick({80,444});check(authored.findObject(doorId)->isOpen(),"Authoring draft survives Play unchanged");
        ecsClick({402,60});check(!authored.findObject(doorId)->isOpen(),"Inspector Apply is a single undo command");

        const auto chestForText=authored.placeObject({54,52},{0,0,0},ObjectType::Container);
        authored.addItem(chestForText,{"wood","Wood",2});
        ecsClick(GetWorldToScreen2D(authored.cellCenter({54,52}),ecsCamera.camera()));
        ecsClick({100,386});check(ecsEditor.capturesKeyboard(),"Inspector text field captures keyboard");
        EditorInput typingUndo;typingUndo.undo=true;ecsUpdate(typingUndo);
        check(authored.findObject(chestForText)->contents().size()==1,"Focused text ignores world undo shortcut");
        std::cout << "Tileset editor interaction and rendering tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    CloseWindow();
    return result;
}
