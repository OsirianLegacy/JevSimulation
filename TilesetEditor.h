#pragma once
#include "SceneWorld.h"
#include "TilesetLibrary.h"
#include "ObjectInspector.h"
#include "CatalogEditor.h"
#include <utility>

enum class MapAction { None, Save, Load };
enum class TransportAction { None, Play, Pause, Step, Stop };
enum class EditorMode { Tiles, Items, ObjectTypes };

// Explicit input keeps editor interactions testable without synthesizing OS input.
struct EditorInput {
    Vector2 mouse{};
    Vector2 wheel{};
    bool leftPressed = false, leftDown = false;
    bool rightPressed = false, rightDown = false;
    bool toggle = false, shift = false, focused = true;
    bool save = false, load = false;
    std::string text;
    bool backspace = false;
    bool undo = false, redo = false;
    static EditorInput read();
};

class TilesetEditor {
public:
    explicit TilesetEditor(const TilesetLibrary& tilesets, bool open=false) : tilesets_(tilesets), open_(open) {}
    void update(SceneWorld& grid, const Camera2D& camera, const EditorInput& input, int screenHeight);
    void draw(int screenHeight) const;
    void drawBrush(const SceneWorld& grid, const Camera2D& camera, Vector2 mouse) const;
    bool capturesMouse(Vector2 mouse) const;
    bool capturesKeyboard() const { return open_ && (mode_ != EditorMode::Tiles || (inspect_ && inspector_.capturesKeyboard())); }
    bool textInputActive() const {return open_ && (mode_!=EditorMode::Tiles?catalogEditor_.capturesKeyboard():inspect_ && inspector_.capturesKeyboard());}
    void clearSelection() { inspector_.clear(); catalogEditor_.reset(); objectTypeId_.clear(); customTypesOpen_=false; }
    EditorMode mode() const { return mode_; }
    bool visible() const { return open_; }
    GridLayer layer() const { return layer_; }
    TileRef selected() const { return selected_; }
    CellPosition selectionSize() const { return selectionSize_; }
    MapAction takeMapAction() { return std::exchange(mapAction_, MapAction::None); }
    TransportAction takeTransportAction(){return std::exchange(transport_,TransportAction::None);}
    void setPlayState(bool active,bool paused){playing_=active;paused_=paused;if(active){mode_=EditorMode::Tiles;layer_=GridLayer::Objects;inspect_=true;}}
    void finishStroke(SceneWorld& world){if(grouping_){world.endEdit();grouping_=false;}stroke_=0;previous_.reset();}
    void refreshInspector(const SceneWorld& world){inspector_.refresh(world);}
    static constexpr int panelWidth = 360;

private:
    const TilesetLibrary& tilesets_;
    bool open_ = false, dropdown_ = false;
    GridLayer layer_ = GridLayer::Ground;
    TileRef selected_{0, 0, 0};
    CellPosition selectionSize_{1, 1}, selectionAnchor_{}, strokeOrigin_{};
    bool selecting_ = false;
    int scale_ = 2, dropdownOffset_ = 0, dragScroll_ = 0, stroke_ = 0;
    Vector2 scroll_{};
    std::optional<CellPosition> previous_;
    MapAction mapAction_ = MapAction::None;
    ObjectType objectType_ = ObjectType::Container;
    bool inspect_ = false;
    EditorMode mode_ = EditorMode::Tiles;
    CatalogEditor catalogEditor_;
    const SceneWorld* grid_ = nullptr;
    std::string objectTypeId_;
    bool customTypesOpen_ = false;
    int customTypeOffset_ = 0;
    ObjectInspector inspector_;
    bool grouping_=false, playing_=false, paused_=false;
    TransportAction transport_=TransportAction::None;
    Rectangle atlas(int height) const;
    void clampScroll(int height);
    void stamp(SceneWorld& grid, CellPosition cell);
    void drawMenu() const;
    void drawTransport() const;
};
