#include "AIDemo.h"
#include "DecisionBridge.h"
#include "NodeConfig.h"
#include "MapStorage.h"
#include "WorldCamera.h"
#include <chrono>
#include <iostream>
#include "MemoryUsage.h"
int runAIDemo(const std::string &configPath, bool stress, bool smoke) {
    auto config=ai::Config::load(configPath);
    if (stress) config.provider="fake"; // Offline by construction.
    SceneWorld world;
    GridCell ground; ground.tile(GridLayer::Ground)={0,0,0};
    world.fillRegion({960,960,1120,1056},ground);
    for (int y=963;y<1048;++y) if (y%12) world.paint({1032,y},GridLayer::Walls,{0,0,0});
    auto player=world.spawnCreature({1000,1000},"player",ControlOwnership::Player);
    const int count=stress?1000:20;
    for (int i=0;i<count;++i) world.spawnCreature(stress ? CellPosition{965+(i%50),965+(i/50)} : CellPosition{980+(i%10)*8,990+(i/10)*24},"wanderer");
    world.setHistoryEnabled(false);
    const auto baseline=memoryBytes();
    const auto start=std::chrono::steady_clock::now();
    {
        auto clone=SceneWorld::fromDocument(world.document());
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout << ai::Json{{"event","world_clone"},{"cells",world.count()},{"creatures",count+1},
            {"cloneMs",ms},{"privateBytesBefore",baseline},{"privateBytesWithClone",memoryBytes()},
            {"terrainBytesPerWorld",world.count()*sizeof(GridCell)}}.dump() << '\n';
    }
    std::unique_ptr<ai::Transport> transport;
    if (config.provider=="proxy") transport=std::make_unique<ai::BridgeTransport>(JevNodeExecutable,JevDecisionBridge,
        JevProjectDirectory,config.proxyUrl,config.timeout);
    ai::DecisionSystem decisions(world,config,std::move(transport));
    if (stress) {
        for (int i=0;i<3600;++i) { world.tick(); decisions.update(1.0/60,i/60.0); }
        std::cout << decisions.metrics().dump() << '\n';
        std::cout << ai::Json{{"event","stress_complete"},{"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()},
            {"privateBytes",memoryBytes()},{"entities",count},{"simulatedSeconds",60}}.dump() << '\n';
        return 0;
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (smoke?FLAG_WINDOW_HIDDEN:0)); InitWindow(1100,700,"Jev AI decision loop");
    if (!IsWindowReady()) throw std::runtime_error("Cannot open AI demo window.");
    struct Close { ~Close() { CloseWindow(); } } close;
    SetTargetFPS(60);
    WorldCamera camera(world.worldBounds()); camera.setZoom(2); camera.resize(GetScreenWidth(),GetScreenHeight());
    // Pan from world center to the fixture.
    const auto center=world.cellCenter({1024,1008});
    const auto initial=camera.camera().target;
    camera.pan({center.x-initial.x,center.y-initial.y},std::hypot(center.x-initial.x,center.y-initial.y)/600.0f);
    bool paused=false; int frames=0; double accumulator=0;
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) paused=!paused;
        if (IsKeyPressed(KEY_R)) decisions.invalidate();
        auto current=world.findCreature(player)->position().cellCoordinates(), destination=current;
        if (IsKeyPressed(KEY_UP)) --destination.y;
        if (IsKeyPressed(KEY_DOWN)) ++destination.y;
        if (IsKeyPressed(KEY_LEFT)) --destination.x;
        if (IsKeyPressed(KEY_RIGHT)) ++destination.x;
        if (!paused && destination!=current) world.moveCreature(player,destination);
        camera.resize(GetScreenWidth(),GetScreenHeight()); camera.update(GetFrameTime());
        if (paused) decisions.update(0,GetTime(),true);
        else {
            accumulator+=std::min<double>(GetFrameTime(),5.0/60);
            while (accumulator>=1.0/60) { world.tick(); decisions.update(1.0/60,GetTime()); accumulator-=1.0/60; }
        }
        BeginDrawing(); ClearBackground({22,26,34,255}); BeginMode2D(camera.camera());
        world.draw(camera.visibleBounds());
        const auto visible=world.visibleRange(camera.visibleBounds());
        for (int y=visible.minY;y<visible.maxY;++y) for(int x=visible.minX;x<visible.maxX;++x)
            if (world.at({x,y}).tile(GridLayer::Walls).present()) DrawRectangleRec(world.cellBounds({x,y}),GRAY);
        EndMode2D();
        DrawRectangle(0,0,GetScreenWidth(),80,{15,19,26,230});
        DrawText("AI demo | WASD camera | arrows player | Space pause | R reset decisions",12,10,18,RAYWHITE);
        DrawText(TextFormat("%s | %s | queued %d | in flight %d",config.provider.c_str(),paused?"paused":"running",
            static_cast<int>(decisions.queuedCount()),decisions.inFlight()),12,38,18,SKYBLUE);
        DrawFPS(GetScreenWidth()-80,60); EndDrawing();
        if (smoke && ++frames==120) break;
    }
    std::cout << decisions.metrics().dump() << '\n'; return 0;
}
