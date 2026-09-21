#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "NodeProcess.h"
#include "NodeConfig.h"
#include "SceneWorld.h"
#include "DecisionBridge.h"
#include "AIDemo.h"
#include "WorldCamera.h"
#include "TilesetEditor.h"
#include "PlaySession.h"
#include "EntityPresentation.h"
#include "PlayerController.h"
#include <raylib.h>

namespace {
struct GameWindow {
    explicit GameWindow(bool hidden) {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | (hidden ? FLAG_WINDOW_HIDDEN : 0));
        InitWindow(1100, 700, "JevSimulation");
        if (!IsWindowReady()) throw std::runtime_error("Could not initialize the game window.");
        SetWindowMinSize(900, 600);
        SetTargetFPS(144);
    }
    ~GameWindow() { CloseWindow(); }
};
}

int main(int argc, char* argv[])
{
    try {
        bool dryRun=false,smokeTest=false,gameMode=false,aiDemo=false,aiStress=false,liveSmoke=false;
        std::string aiConfig=JevAIConfig;
        for(int i=1;i<argc;++i){
            const std::string_view argument(argv[i]);
            if(argument=="--dry-run")dryRun=true;
            else if(argument=="--window-smoke-test")smokeTest=true;
            else if(argument=="--game")gameMode=true;
            else if(argument=="--ai-demo")aiDemo=true;
            else if(argument=="--ai-stress")aiStress=true;
            else if(argument=="--jev-smoke-test")liveSmoke=true;
            else if(argument=="--ai-config" && i+1<argc)aiConfig=argv[++i];
            else throw std::runtime_error("Usage: JevSimulation [--game | --ai-demo | --ai-stress] [--ai-config path] [--window-smoke-test] | --dry-run | --jev-smoke-test");
        }
        if(aiDemo || aiStress) return runAIDemo(aiConfig,aiStress,smokeTest);
        auto decisionConfig=ai::Config::load(aiConfig);
        if (smokeTest) decisionConfig.provider="fake";
        if(dryRun && (gameMode || smokeTest))throw std::runtime_error("--dry-run is a standalone headless mode.");
        std::vector<std::wstring> arguments{JevTestScript};
        if (dryRun || smokeTest) arguments.emplace_back(L"--dry-run");
        if (dryRun || liveSmoke) {
            NodeProcess node(JevNodeExecutable, arguments, JevProjectDirectory);
            return node.wait();
        }

        GameWindow window(smokeTest);
        EntityPresentation presentation(std::filesystem::path(JevProjectDirectory)/"Assets");
        bool gamePaused=false;
        SceneWorld grid;
        std::unique_ptr<ai::DecisionSystem> decisions;
        SceneWorld *decisionWorld=nullptr;
        double lastDecisionTime=0;
        PlayerController players;
        TilesetLibrary tilesets(JevTilesetDirectory);
        TilesetEditor authorEditor(tilesets,true);
        std::unique_ptr<TilesetEditor> runtimeEditor;
        PlaySession play;
        FixedStep gameStepper;
        std::optional<WorldCamera> authorCamera;
        WorldCamera worldCamera(grid.worldBounds());
        worldCamera.setZoom(3);
        worldCamera.resize(GetScreenWidth(), GetScreenHeight());
        const auto catalog = tilesets.catalog();
        std::string mapStatus = gameMode?"Game simulation | Maps/world.jevmap":"Ctrl+S Save  |  Ctrl+O Load  |  Maps/world.jevmap";
        bool mapError = false;
        const auto mapAction = [&](MapAction action) {
            try {
                if (action == MapAction::Save) {
                    authorEditor.finishStroke(grid);
                    saveMap(JevMapFile, grid, catalog);
                    grid.markSaved();
                    mapStatus = "Saved Maps/world.jevmap";
                } else {
                    decisions.reset(); decisionWorld=nullptr; players.clear();
                    auto loaded = loadMap(JevMapFile, catalog);
                    grid = std::move(loaded);
                    grid.setHistoryEnabled(true);
                    authorEditor.clearSelection();
                    worldCamera = WorldCamera(grid.worldBounds());
                    worldCamera.setZoom(3);
                    worldCamera.resize(GetScreenWidth(), GetScreenHeight());
                    mapStatus = "Loaded Maps/world.jevmap";
                }
                mapError = false;
                std::cout << "[INFO] " << mapStatus << std::endl;
            } catch (const std::exception& error) {
                mapError = true;
                mapStatus = std::string(action == MapAction::Save ? "Save failed: " : "Load failed: ") + error.what();
                std::cerr << "[ERROR] " << mapStatus << std::endl;
            }
        };
        std::error_code mapFileError;
        if (!smokeTest && std::filesystem::exists(JevMapFile, mapFileError)) mapAction(MapAction::Load);
        if (smokeTest) {
            GridCell floor; floor.tile(GridLayer::Ground)={0,0,0};
            grid.fillRegion({1020,1020,1032,1032},floor);
            grid.spawnCreature({1024,1024},"player",ControlOwnership::Player);
            grid.spawnCreature({1026,1024}); grid.spawnCreature({1027,1026});
        }
        grid.setHistoryEnabled(true);

        std::unique_ptr<NodeProcess> node;
        std::optional<int> result = smokeTest ? std::nullopt : std::optional(0);
        try {
            if (smokeTest) node = std::make_unique<NodeProcess>(JevNodeExecutable, arguments, JevProjectDirectory);
        } catch (const std::exception& error) {
            std::cerr << "[ERROR] Node startup: " << error.what() << std::endl;
            result = 1;
        }

        bool smokePlayStarted=false,smokePlayStopped=false;
        std::uint64_t smokeDecisionRequests=0;
        int frames = 0;
        int framesAfterResult = 0;
        while (!WindowShouldClose()) {
            bool singleStep=false;
            auto input = EditorInput::read();
            if(smokeTest && !gameMode && (frames==10 || frames==30 || frames==31 || frames==50)){
                input.mouse={frames==10?570.0f:frames==30?652.0f:frames==31?740.0f:830.0f,60};
                input.leftPressed=input.leftDown=true;
            }
            auto* activeWorld=play.active()?&play.world():&grid;
            auto* editor=play.active()?runtimeEditor.get():&authorEditor;
            if (gameMode && IsKeyPressed(KEY_SPACE)) gamePaused=!gamePaused;
            worldCamera.resize(GetScreenWidth(), GetScreenHeight());
            if (gameMode || !editor->capturesKeyboard()) worldCamera.update(GetFrameTime(), gameMode || !editor->capturesMouse(input.mouse));
            if(!gameMode){
                editor->setPlayState(play.active(),play.paused());
                editor->update(*activeWorld,worldCamera.camera(),input,GetScreenHeight());
                const auto requested=editor->takeMapAction();
                if(!play.active() && !smokeTest && requested!=MapAction::None)mapAction(requested);
                const auto transport=editor->takeTransportAction();
                try {
                    if(transport==TransportAction::Play && !play.active()){
                        authorEditor.finishStroke(grid);play.start(grid);authorCamera=worldCamera;smokePlayStarted=true;
                        runtimeEditor=std::make_unique<TilesetEditor>(tilesets,true);runtimeEditor->setPlayState(true,false);
                    } else if(transport==TransportAction::Pause)play.pause(!play.paused());
                    else if(transport==TransportAction::Step){singleStep=play.paused();play.step();}
                    else if(transport==TransportAction::Stop && play.active()){
                        decisions.reset(); decisionWorld=nullptr; players.clear();
                        play.stop(grid);smokePlayStopped=true;runtimeEditor.reset();worldCamera=*authorCamera;authorCamera.reset();
                    }
                }catch(const std::exception& error){mapStatus=error.what();mapError=true;}
                activeWorld=play.active()?&play.world():&grid;
                editor=play.active()?runtimeEditor.get():&authorEditor;
                editor->setPlayState(play.active(),play.paused());
                play.advance(GetFrameTime());
            }else if (!gamePaused) gameStepper.advance(grid,GetFrameTime());
            if (gameMode || play.active()) {
                if (decisionWorld != activeWorld) {
                    players.clear();
                    decisions.reset(); std::unique_ptr<ai::Transport> transport;
                    if (decisionConfig.provider=="proxy") transport=std::make_unique<ai::BridgeTransport>(
                        JevNodeExecutable,JevDecisionBridge,JevProjectDirectory,decisionConfig.proxyUrl,decisionConfig.timeout,
                        decisionConfig.managedProxy ? std::filesystem::absolute(aiConfig).wstring() : std::wstring{});
                    decisions=std::make_unique<ai::DecisionSystem>(*activeWorld,decisionConfig,std::move(transport));
                    decisionWorld=activeWorld; lastDecisionTime=activeWorld->simulationTime();
                }
                const auto time=activeWorld->simulationTime();
                PlayerInput playerInput;
                playerInput.mouse=input.mouse;playerInput.leftPressed=input.leftPressed;playerInput.leftDown=input.leftDown;
                playerInput.leftReleased=IsMouseButtonReleased(MOUSE_BUTTON_LEFT);playerInput.rightPressed=input.rightPressed;
                playerInput.shift=input.shift;playerInput.cancel=IsKeyPressed(KEY_X);playerInput.focused=input.focused;
                playerInput.keyboardInput=gameMode || !editor->textInputActive();
                playerInput.worldInput=playerInput.keyboardInput && (gameMode || !editor->capturesMouse(input.mouse));
                players.update(*activeWorld,*decisions,worldCamera.camera(),playerInput,GetFrameTime());
                const bool paused=gameMode?gamePaused:play.paused();
                if (time > lastDecisionTime || paused)
                    decisions->update(std::max(0.0,time-lastDecisionTime),GetTime(),paused);
                if (singleStep) decisions->step(std::max(0.0,time-lastDecisionTime));
                lastDecisionTime=time;
            }
            if (smokeTest) worldCamera.pan({1, 1}, 1.0f / 60);
            // Update: poll once per frame; network I/O never blocks the game.
            if (node && !result) {
                try {
                    result = node->poll();
                    if (result) {
                        if (*result != 0) {
                            std::cerr << "[ERROR] Jev Node process exited with code " << *result << ".\n";
                        }
                        std::cout << "[INFO] Node check finished; game loop remains active." << std::endl;
                    }
                } catch (const std::exception& error) {
                    std::cerr << "[ERROR] Node status: " << error.what() << std::endl;
                    result = 1;
                }
            }

            // Draw: the window remains open after either success or failure.
            BeginDrawing();
            ClearBackground(Color{22, 26, 34, 255});
            BeginMode2D(worldCamera.camera());
            activeWorld->draw(worldCamera.visibleBounds(), &tilesets, &presentation);
            const auto hovered = activeWorld->worldToCell(GetScreenToWorld2D(GetMousePosition(), worldCamera.camera()));
            if (hovered && (gameMode || !editor->capturesMouse(input.mouse))) presentation.drawHover(activeWorld->cellBounds(*hovered),GetTime());
            if(decisions)players.drawWorld(*activeWorld,worldCamera.camera(),*decisions);
            if(!gameMode && editor->layer()!=GridLayer::Entities)editor->drawBrush(*activeWorld, worldCamera.camera(), input.mouse);
            EndMode2D();
            if(decisions)players.drawScreen(worldCamera.camera());
            const int hudX = !gameMode && editor->visible() ? TilesetEditor::panelWidth + 12 : 12;
            const int hudY = !gameMode && editor->visible() ? 90 : 12;
            DrawRectangle(hudX, hudY, 410, 70, Color{15, 19, 26, 225});
            DrawText(gameMode?"Game  |  WASD Pan  |  Wheel Zoom":"F1 Editor  |  WASD Pan  |  Wheel Zoom", hudX + 12, hudY + 10, 18, RAYWHITE);
            if (hovered && (gameMode || !editor->capturesMouse(input.mouse)))
                DrawText(TextFormat("Cell %d, %d  |  %s", hovered->x, hovered->y,
                    activeWorld->isWalkable(*hovered) ? "Walkable" : "Not walkable"), hudX + 12, hudY + 38, 16, SKYBLUE);
            if(decisions) {
                std::string status=std::to_string(players.selection().size())+" selected | Drag / Shift: select | Right-click: move / harvest / attack | X: stop";
                DrawRectangle(hudX,hudY+108,GetScreenWidth()-hudX-12,26,{15,19,26,225});
                BeginScissorMode(hudX+8,hudY+110,GetScreenWidth()-hudX-28,22);
                DrawText(status.c_str(),hudX+8,hudY+113,14,LIGHTGRAY);EndScissorMode();
                DrawRectangle(hudX,hudY+138,GetScreenWidth()-hudX-12,24,{15,19,26,225});
                BeginScissorMode(hudX+8,hudY+139,GetScreenWidth()-hudX-28,22);
                DrawText(players.status().c_str(),hudX+8,hudY+143,14,LIGHTGRAY);EndScissorMode();
            }
            if(!gameMode)editor->draw(GetScreenHeight());
            if (smokeTest && decisions) {
                const auto stats=decisions->metrics();
                std::uint64_t requests=0;
                for (const auto &band:stats.at("bands")) requests+=band.at("requests").get<std::uint64_t>();
                smokeDecisionRequests=std::max(smokeDecisionRequests,requests);
            }
            DrawRectangle(hudX, GetScreenHeight() - 32, GetScreenWidth() - hudX - 12, 24, Color{15, 19, 26, 225});
            BeginScissorMode(hudX + 8, GetScreenHeight() - 30, GetScreenWidth() - hudX - 28, 22);
            DrawText((play.active()?std::string(play.paused()?"Play paused | ":"Play running | ")+std::to_string(activeWorld->ticks())+" ticks | Runtime changes discarded on Stop":(grid.dirty()?"* Unsaved | ":"")+mapStatus).c_str(), hudX + 8, GetScreenHeight() - 28, 16, mapError ? RED : LIGHTGRAY);
            EndScissorMode();
            DrawFPS(GetScreenWidth()-80, !gameMode && editor->visible() ? 86 : 0);
            if (gameMode) DrawText(gamePaused?"PAUSED | Space: resume":"Space: pause to inspect entities",12,GetScreenHeight()-56,16,SKYBLUE);
            presentation.drawTooltip(*activeWorld,(gameMode || !editor->capturesMouse(input.mouse))?hovered:std::nullopt,
                gameMode?gamePaused:(play.active() && play.paused()),input.mouse);
            EndDrawing();

            if (result) ++framesAfterResult;
            // Only the explicit offline smoke-test mode closes automatically.
            if (smokeTest && ++frames >= 120) break;
        }
        if(smokeTest && !gameMode && (!smokePlayStarted || !smokePlayStopped || play.active()))throw std::runtime_error("Play smoke test failed to stop.");
        if(smokeTest && gameMode && grid.ticks()==0)throw std::runtime_error("Game mode did not simulate.");
        if(smokeTest && !smokeDecisionRequests) throw std::runtime_error("Main executable did not dispatch AI decisions.");
        if(decisions) std::cout << decisions->metrics().dump() << std::endl;
        decisions.reset();
        if(play.active())play.stop(grid);
        if (smokeTest && (!result || *result != 0 || framesAfterResult < 30)) {
            throw std::runtime_error("Window smoke test did not keep drawing after Node completed.");
        }
        if (!result) std::cout << "[INFO] Window closed; cancelling pending Jev check." << std::endl;
        node.reset();
        std::cout << "[INFO] JevSimulation closing; Node stopped." << std::endl;
        return result.value_or(0);
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] JevSimulation: " << error.what() << '\n';
        return 1;
    }
}
