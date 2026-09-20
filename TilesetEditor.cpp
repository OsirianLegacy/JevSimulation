#include "TilesetEditor.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr Color background{20, 25, 33, 255}, surface{35, 43, 54, 255}, border{66, 80, 97, 255};
constexpr Color accent{81, 204, 193, 255}, muted{155, 168, 183, 255};
constexpr Rectangle dropdownButton{16, 50, 328, 34};
bool inside(Vector2 point, Rectangle rect) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}
void button(Rectangle rect, const char* text, bool selected = false, bool disabled = false) {
    DrawRectangleRec(rect, selected ? Color{32, 81, 81, 255} : surface);
    DrawRectangleLinesEx(rect, 1, selected ? accent : border);
    const int font = 16;
    DrawText(text, static_cast<int>(rect.x + (rect.width - MeasureText(text, font)) / 2),
             static_cast<int>(rect.y + (rect.height - font) / 2), font, disabled ? Color{94, 109, 122, 255} : RAYWHITE);
}
Rectangle verticalTrack(Rectangle view) { return {334, view.y, 10, view.height}; }
Rectangle horizontalTrack(Rectangle view) { return {view.x, view.y + view.height + 6, view.width, 10}; }
void scrollBar(Rectangle track, float offset, float content, float view, bool vertical) {
    DrawRectangleRec(track, surface);
    const float length = vertical ? track.height : track.width;
    const float thumb = content <= view ? length : std::max(18.0f, length * view / content);
    const float position = content <= view ? 0 : (length - thumb) * offset / (content - view);
    if (vertical) DrawRectangleRec({track.x, track.y + position, track.width, thumb}, border);
    else DrawRectangleRec({track.x + position, track.y, thumb, track.height}, border);
}
}

EditorInput EditorInput::read() {
    const bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    EditorInput input{GetMousePosition(), GetMouseWheelMoveV(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
        IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsMouseButtonPressed(MOUSE_BUTTON_RIGHT),
        IsMouseButtonDown(MOUSE_BUTTON_RIGHT), IsKeyPressed(KEY_F1),
        IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), IsWindowFocused(),
        control && IsKeyPressed(KEY_S), control && IsKeyPressed(KEY_O)};
    for (int codepoint = GetCharPressed(); codepoint; codepoint = GetCharPressed()) {
        if (!control && codepoint >= 32) {
            int size = 0; const char* text = CodepointToUTF8(codepoint, &size); input.text.append(text, size);
        }
    }
    input.backspace = IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE);
    input.undo=control && IsKeyPressed(KEY_Z);
    input.redo=control && IsKeyPressed(KEY_Y);
    return input;
}

Rectangle TilesetEditor::atlas(int height) const {
    const int top = layer_ == GridLayer::Objects ? 294 : 214;
    return {16, static_cast<float>(top), 312, static_cast<float>(std::max(32, height - top - 158))};
}
bool TilesetEditor::capturesMouse(Vector2 mouse) const {
    return open_ && (mode_ != EditorMode::Tiles || dropdown_ || customTypesOpen_ ||
        (mouse.y >= 0 && mouse.y < 82) || (mouse.x >= 0 && mouse.x < panelWidth));
}
void TilesetEditor::clampScroll(int height) {
    const auto* sheet = tilesets_.get(selected_.tileset);
    const auto view = atlas(height);
    scroll_.x = std::clamp(scroll_.x, 0.0f, sheet ? std::max(0.0f, sheet->texture.width * scale_ - view.width) : 0);
    scroll_.y = std::clamp(scroll_.y, 0.0f, sheet ? std::max(0.0f, sheet->texture.height * scale_ - view.height) : 0);
}

void TilesetEditor::stamp(SceneWorld& grid, CellPosition end) {
    if (!previous_) strokeOrigin_ = end;
    const auto toStamp = [&](CellPosition cell) {
        return CellPosition{static_cast<int>(std::floor(static_cast<double>(cell.x-strokeOrigin_.x)/selectionSize_.x)),
                            static_cast<int>(std::floor(static_cast<double>(cell.y-strokeOrigin_.y)/selectionSize_.y))};
    };
    auto current = toStamp(previous_.value_or(end));
    end = toStamp(end);
    // Interpolate the stroke in cell space so fast drags do not leave gaps.
    const int dx = std::abs(end.x - current.x), dy = -std::abs(end.y - current.y);
    const int sx = current.x < end.x ? 1 : -1, sy = current.y < end.y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        for (int y=0; y<selectionSize_.y; ++y) for (int x=0; x<selectionSize_.x; ++x) {
            const CellPosition cell{strokeOrigin_.x+current.x*selectionSize_.x+x, strokeOrigin_.y+current.y*selectionSize_.y+y};
            if (!grid.contains(cell)) continue;
            const TileRef tile{selected_.tileset,selected_.column+x,selected_.row+y};
            if (stroke_ == 2) grid.erase(cell, layer_);
            else if (layer_ == GridLayer::Objects) {
                if (objectTypeId_.empty()) grid.placeObject(cell,tile,objectType_);
                else grid.placeObject(cell,tile,objectTypeId_);
            }
            else grid.paint(cell, layer_, tile);
        }
        if (current == end) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; current.x += sx; }
        if (twice <= dx) { error += dx; current.y += sy; }
    }
    previous_ = CellPosition{strokeOrigin_.x+end.x*selectionSize_.x,strokeOrigin_.y+end.y*selectionSize_.y};
}

void TilesetEditor::update(SceneWorld& grid, const Camera2D& camera, const EditorInput& input, int height) {
    grid_ = &grid;
    const auto types = grid.objectTypes();
    if(grouping_ && (input.toggle || !input.focused || input.save || input.load || capturesMouse(input.mouse) ||
       (stroke_==1 && !input.leftDown) || (stroke_==2 && !input.rightDown)))finishStroke(grid);
    if(!objectTypeId_.empty() && !types.contains(objectTypeId_))objectTypeId_.clear();
    if(input.focused && !textInputActive() && (!playing_ || paused_) && (input.undo || input.redo)) {
        finishStroke(grid);if(input.undo)grid.undo();else grid.redo();inspector_.refresh(grid);return;
    }
    if(open_ && input.focused && input.mouse.x>=panelWidth && input.mouse.y>=44 && input.mouse.y<82){
        if(input.leftPressed) {
            const int choice=static_cast<int>((input.mouse.x-372)/84);
            if(input.mouse.x>=372 && input.mouse.x<876) switch(choice){
                case 0:if(!playing_ || paused_){grid.undo();inspector_.refresh(grid);}break;
                case 1:if(!playing_ || paused_){grid.redo();inspector_.refresh(grid);}break;
                case 2:if(!playing_)transport_=TransportAction::Play;break;
                case 3:if(playing_)transport_=TransportAction::Pause;break;
                case 4:if(playing_ && paused_)transport_=TransportAction::Step;break;
                case 5:if(playing_)transport_=TransportAction::Stop;break;
            }
        }
        return;
    }
    if(playing_) {
        if(input.toggle){open_=!open_;return;}
        if(!open_ || !input.focused)return;
        if(input.leftPressed && !capturesMouse(input.mouse) && input.mouse.x>=panelWidth && input.mouse.y>=82){
            const auto cell=grid.worldToCell(GetScreenToWorld2D(input.mouse,camera));
            if(cell)inspector_.select(grid,*cell);else inspector_.clear();
        }
        inspector_.update(grid,input,height,!paused_);return;
    }
    if (input.focused && (input.save || input.load)) {
        mapAction_ = input.save ? MapAction::Save : MapAction::Load;
        stroke_ = dragScroll_ = 0; previous_.reset(); dropdown_ = selecting_ = customTypesOpen_ = false;
        return;
    }
    if (input.toggle) { open_ = !open_; dropdown_ = selecting_ = customTypesOpen_ = false; stroke_ = 0; previous_.reset(); inspector_.clear(); catalogEditor_.reset(); }
    if (!open_ || !input.focused) { stroke_ = 0; dragScroll_ = 0; selecting_ = false; previous_.reset(); return; }
    if (input.mouse.y >= 0 && input.mouse.y < 82 && input.mouse.x >= panelWidth) {
        stroke_ = 0; previous_.reset();
        if (input.leftPressed) for (int i=0;i<3;++i) {
            if (inside(input.mouse,{372.0f+172*i,8,164,30})) {
                mode_=static_cast<EditorMode>(i); inspector_.clear(); catalogEditor_.reset();
                dropdown_=selecting_=customTypesOpen_=false; dragScroll_=0;
            }
        }
        return;
    }
    if (mode_ != EditorMode::Tiles) {
        if (catalogEditor_.update(grid,input,mode_==EditorMode::ObjectTypes,height)) mapAction_=MapAction::Save;
        return;
    }
    if (customTypesOpen_) {
        const int count=static_cast<int>(types.size());
        const int visible=std::min(5,count);
        const Rectangle list{16,274,328,static_cast<float>(visible*30)};
        if(inside(input.mouse,list)) customTypeOffset_=std::clamp(customTypeOffset_-static_cast<int>(input.wheel.y),0,std::max(0,count-visible));
        if(input.leftPressed) {
            if(inside(input.mouse,list)) {
                auto found=types.begin();
                std::advance(found,customTypeOffset_+static_cast<int>((input.mouse.y-274)/30));
                if(found!=types.end()) { objectTypeId_=found->first; objectType_=found->second.behavior; }
            }
            customTypesOpen_=false;
        }
        stroke_=0; previous_.reset(); return;
    }
    clampScroll(height);
    if (!input.leftDown) dragScroll_ = 0;
    const int rows = std::min(6, tilesets_.count());
    if (dropdown_) {
        stroke_ = 0; previous_.reset();
        const Rectangle list{16, 84, 328, static_cast<float>(rows * 32)};
        if (inside(input.mouse, list)) {
            dropdownOffset_ = std::clamp(dropdownOffset_ - static_cast<int>(input.wheel.y), 0, std::max(0, tilesets_.count() - rows));
        }
        if (input.leftPressed) {
            if (inside(input.mouse, list)) {
                selected_ = {dropdownOffset_ + static_cast<int>((input.mouse.y - list.y) / 32), 0, 0};
                selectionSize_ = {1,1};
                scroll_ = {};
            }
            dropdown_ = false;
        }
        return; // Dismissal and selection must never paint through the UI.
    }
    const auto view = atlas(height);
    const auto* sheet = tilesets_.get(selected_.tileset);
    if (selecting_ && !input.leftPressed) {
        if (input.leftDown && sheet && inside(input.mouse, view)) {
            const int column = std::clamp(static_cast<int>((input.mouse.x-view.x+scroll_.x)/(8*scale_)),0,sheet->columns-1);
            const int row = std::clamp(static_cast<int>((input.mouse.y-view.y+scroll_.y)/(8*scale_)),0,sheet->rows-1);
            selected_.column = std::min(column,selectionAnchor_.x); selected_.row = std::min(row,selectionAnchor_.y);
            selectionSize_ = {std::abs(column-selectionAnchor_.x)+1,std::abs(row-selectionAnchor_.y)+1};
        }
        if (!input.leftDown) selecting_ = false;
        return; // An atlas drag never turns into a world stroke.
    }
    if (capturesMouse(input.mouse) || dragScroll_) {
        stroke_ = 0; previous_.reset();
        if (input.leftPressed) {
            if (inside(input.mouse, {282, 12, 62, 26})) { open_ = false; return; }
            if (inside(input.mouse, {16, static_cast<float>(height - 132), 158, 26})) { mapAction_ = MapAction::Save; return; }
            if (inside(input.mouse, {184, static_cast<float>(height - 132), 160, 26})) { mapAction_ = MapAction::Load; return; }
            if (inside(input.mouse, dropdownButton) && tilesets_.count()) { dropdown_ = true; return; }
            if (inside(input.mouse, {16, 108, 158, 30})) { layer_ = GridLayer::Ground; inspect_ = false; inspector_.clear(); return; }
            if (inside(input.mouse, {184, 108, 160, 30})) { layer_ = GridLayer::Walls; inspect_ = false; inspector_.clear(); return; }
            if (inside(input.mouse, {16, 144, 158, 22})) { layer_ = GridLayer::Objects; return; }
            if (layer_ == GridLayer::Objects) {
                if (inside(input.mouse, {16,176,158,28})) { inspect_ = false; inspector_.clear(); return; }
                if (inside(input.mouse, {184,176,160,28})) { inspect_ = true; return; }
                if (!inspect_) for (int type = 0; type < 3; ++type)
                    if (inside(input.mouse, {16.0f+type*110,214,106,28})) { objectType_ = static_cast<ObjectType>(type); objectTypeId_.clear(); return; }
                if(!inspect_ && inside(input.mouse,{16,246,328,28}) && !types.empty()) {
                    customTypesOpen_=true; customTypeOffset_=0; return;
                }
            } else {
                if (inside(input.mouse, {246, 176, 28, 28})) { scale_ = std::max(1, scale_ - 1); clampScroll(height); }
                if (inside(input.mouse, {282, 176, 28, 28})) { scale_ = std::min(4, scale_ + 1); clampScroll(height); }
            }
            if (inspect_) { inspector_.update(grid, input, height); return; }
            if (inside(input.mouse, verticalTrack(view))) dragScroll_ = 1;
            if (inside(input.mouse, horizontalTrack(view))) dragScroll_ = 2;
            if (sheet && inside(input.mouse, view)) {
                TileRef tile{selected_.tileset,
                    static_cast<int>((input.mouse.x - view.x + scroll_.x) / (8 * scale_)),
                    static_cast<int>((input.mouse.y - view.y + scroll_.y) / (8 * scale_))};
                if (tilesets_.valid(tile)) {
                    selected_ = tile; selectionSize_ = {1,1};
                    selectionAnchor_ = {tile.column,tile.row}; selecting_ = true;
                }
            }
        }
        if (inspect_) { inspector_.update(grid, input, height); return; }
        if (sheet && dragScroll_ && input.leftDown) {
            const bool vertical = dragScroll_ == 1;
            const auto track = vertical ? verticalTrack(view) : horizontalTrack(view);
            const float content = (vertical ? sheet->texture.height : sheet->texture.width) * scale_;
            const float length = vertical ? track.height : track.width;
            const float thumb = content <= length ? length : std::max(18.0f, length * length / content);
            const float point = vertical ? input.mouse.y - track.y : input.mouse.x - track.x;
            const float fraction = length > thumb ? std::clamp((point - thumb / 2) / (length - thumb), 0.0f, 1.0f) : 0;
            (vertical ? scroll_.y : scroll_.x) = fraction * std::max(0.0f, content - length);
        }
        if (inside(input.mouse, view)) {
            scroll_.x -= (input.wheel.x + (input.shift ? input.wheel.y : 0)) * 48;
            if (!input.shift) scroll_.y -= input.wheel.y * 48;
        }
        clampScroll(height);
        return;
    }
    if (inspect_) {
        stroke_ = 0; previous_.reset();
        const auto cell = grid.worldToCell(GetScreenToWorld2D(input.mouse, camera));
        if (input.leftPressed) { if (cell) inspector_.select(grid, *cell); else inspector_.clear(); }
        if (input.rightPressed && cell) grid.removeObject(*cell);
        inspector_.update(grid, input, height);
        return;
    }
    if (input.leftPressed) { stroke_ = 1; previous_.reset(); }
    if (input.rightPressed) { stroke_ = 2; previous_.reset(); }
    if ((stroke_ == 1 && !input.leftDown) || (stroke_ == 2 && !input.rightDown)) stroke_ = 0;
    if (!stroke_) { previous_.reset(); return; }
    const auto cell = grid.worldToCell(GetScreenToWorld2D(input.mouse, camera));
    if (!cell || (stroke_ == 1 && !tilesets_.valid(selected_))) { previous_.reset(); return; }
    if(!grouping_){grid.beginEdit();grouping_=true;}
    stamp(grid, *cell);
}

void TilesetEditor::drawBrush(const SceneWorld& grid, const Camera2D& camera, Vector2 mouse) const {
    if (!open_ || mode_!=EditorMode::Tiles || inspect_ || selecting_ || capturesMouse(mouse)) return;
    auto cell = grid.worldToCell(GetScreenToWorld2D(mouse, camera));
    if (!cell) return;
    if(stroke_ && previous_) {
        cell->x=strokeOrigin_.x+static_cast<int>(std::floor(static_cast<double>(cell->x-strokeOrigin_.x)/selectionSize_.x))*selectionSize_.x;
        cell->y=strokeOrigin_.y+static_cast<int>(std::floor(static_cast<double>(cell->y-strokeOrigin_.y)/selectionSize_.y))*selectionSize_.y;
    }
    for (int y=0;y<selectionSize_.y;++y) for(int x=0;x<selectionSize_.x;++x) {
        const CellPosition target{cell->x+x,cell->y+y};
        if (!grid.contains(target)) continue;
        const auto bounds = grid.cellBounds(target);
        tilesets_.draw({selected_.tileset,selected_.column+x,selected_.row+y},bounds,Color{255,255,255,150});
        DrawRectangleLinesEx(bounds,1.0f/camera.zoom,accent);
    }
}

void TilesetEditor::drawTransport() const {
    DrawRectangle(panelWidth,44,std::max(0,GetScreenWidth()-panelWidth),38,background);
    const char* labels[]{"Undo","Redo","Play",paused_?"Resume":"Pause","Step","Stop"};
    const bool enabled[]{grid_ && grid_->canUndo() && (!playing_ || paused_),grid_ && grid_->canRedo() && (!playing_ || paused_),!playing_,playing_,playing_ && paused_,playing_};
    for(int i=0;i<6;++i)button({372.0f+i*84,48,78,28},labels[i],false,!enabled[i]);
}
void TilesetEditor::drawMenu() const {
    DrawRectangle(panelWidth,0,std::max(0,GetScreenWidth()-panelWidth),44,background);
    const char* labels[]{"Tile Painting","Item Creation","Object Types"};
    for(int i=0;i<3;++i) button({372.0f+172*i,8,164,30},labels[i],mode_==static_cast<EditorMode>(i),playing_);
}
void TilesetEditor::draw(int height) const {
    if (!open_) return;
    if(playing_){
        DrawRectangle(0,0,panelWidth,height,background);
        DrawText(paused_?"PLAY: PAUSED":"PLAY: RUNNING",16,17,20,SKYBLUE);
        DrawText(paused_?"Runtime edits are discarded on Stop.":"Pause to edit runtime properties.",16,62,14,LIGHTGRAY);
        DrawText("Click an object to inspect",16,108,16,LIGHTGRAY);
        DrawText("Disk Save / Load disabled during Play",16,148,14,GRAY);
        inspector_.draw(height);drawMenu();drawTransport();return;
    }
    if (mode_!=EditorMode::Tiles && grid_) {
        catalogEditor_.draw(*grid_,mode_==EditorMode::ObjectTypes,height); drawMenu(); drawTransport(); return;
    }
    DrawRectangle(0, 0, panelWidth, height, background);
    DrawLine(panelWidth - 1, 0, panelWidth - 1, height, border);
    DrawText("TILESET EDITOR", 16, 17, 20, RAYWHITE);
    button({282, 12, 62, 26}, "F1  x");
    const auto* sheet = tilesets_.get(selected_.tileset);
    DrawRectangleRec(dropdownButton, surface);
    DrawRectangleLinesEx(dropdownButton, 1, border);
    BeginScissorMode(24, 54, 278, 28);
    DrawText(sheet ? sheet->name.c_str() : "No PNG tilesets found", 26, 60, 18, RAYWHITE);
    EndScissorMode();
    DrawText(dropdown_ ? "^" : "v", 322, 60, 18, accent);
    DrawText("PAINT LAYER", 16, 91, 12, muted);
    button({16, 108, 158, 30}, "Ground", layer_ == GridLayer::Ground);
    button({184, 108, 160, 30}, "Walls", layer_ == GridLayer::Walls);
    button({16, 144, 158, 22}, "Objects", layer_ == GridLayer::Objects);
    button({184, 144, 160, 22}, "Entities (reserved)", false, true);
    if (layer_ == GridLayer::Objects) {
        button({16,176,158,28}, "Paint", !inspect_); button({184,176,160,28}, "Inspect", inspect_);
        if (!inspect_) {
            const char* types[]{"Door", "Container", "Gatherable"};
            for (int type = 0; type < 3; ++type) button({16.0f+type*110,214,106,28}, types[type], objectTypeId_.empty() && objectType_ == static_cast<ObjectType>(type));
            std::string custom="Custom type...";
            if(grid_ && grid_->objectTypes().contains(objectTypeId_)) custom=grid_->objectTypes().at(objectTypeId_).displayName;
            BeginScissorMode(16,246,328,28);
            button({16,246,328,28},custom.c_str(),!objectTypeId_.empty(),!grid_ || grid_->objectTypes().empty());
            EndScissorMode();
        }
    } else {
        DrawText("8 x 8 TILES", 16, 185, 14, muted);
        button({246, 176, 28, 28}, "-"); button({282, 176, 28, 28}, "+");
        DrawText(TextFormat("%dx", scale_), 319, 183, 16, accent);
    }

    if (!inspect_) {
    const auto view = atlas(height);
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y), static_cast<int>(view.width), static_cast<int>(view.height));
    for (int y = static_cast<int>(view.y); y < view.y + view.height; y += 16)
        for (int x = static_cast<int>(view.x); x < view.x + view.width; x += 16)
            DrawRectangle(x, y, 16, 16, ((x / 16 + y / 16) % 2) ? Color{43, 49, 59, 255} : Color{34, 40, 49, 255});
    if (sheet) {
        const Vector2 origin{view.x - scroll_.x, view.y - scroll_.y};
        DrawTextureEx(sheet->texture, origin, 0, static_cast<float>(scale_), WHITE);
        const float tile = static_cast<float>(8 * scale_);
        const int firstX = static_cast<int>(scroll_.x / tile), firstY = static_cast<int>(scroll_.y / tile);
        for (int x = firstX; x <= std::min(sheet->columns, firstX + static_cast<int>(view.width / tile) + 1); ++x)
            DrawLineV({origin.x + x * tile, view.y}, {origin.x + x * tile, std::min(view.y + view.height, origin.y + sheet->texture.height * scale_)}, Color{220, 232, 242, 35});
        for (int y = firstY; y <= std::min(sheet->rows, firstY + static_cast<int>(view.height / tile) + 1); ++y)
            DrawLineV({view.x, origin.y + y * tile}, {std::min(view.x + view.width, origin.x + sheet->texture.width * scale_), origin.y + y * tile}, Color{220, 232, 242, 35});
        DrawRectangleLinesEx({origin.x + selected_.column * tile, origin.y + selected_.row * tile,
                             selectionSize_.x*tile, selectionSize_.y*tile}, 2, accent);
    } else DrawText("Add tilesheets to Assets/Tilesets", 24, 230, 16, muted);
    EndScissorMode();
    DrawRectangleLinesEx(view, 1, border);
    scrollBar(verticalTrack(view), scroll_.y, sheet ? sheet->texture.height * scale_ : 0, view.height, true);
    scrollBar(horizontalTrack(view), scroll_.x, sheet ? sheet->texture.width * scale_ : 0, view.width, false);
    }
    button({16, static_cast<float>(height - 132), 158, 26}, "Save  Ctrl+S");
    button({184, static_cast<float>(height - 132), 160, 26}, "Load  Ctrl+O");
    DrawText(inspect_ ? "Wheel: scroll properties and contents" : "Wheel: scroll   Shift+wheel: sideways", 16, height - 94, 14, muted);
    DrawRectangle(16, height - 72, 48, 48, surface);
    tilesets_.draw(selected_, {16, static_cast<float>(height - 72), 48, 48});
    DrawRectangleLines(16, height - 72, 48, 48, accent);
    DrawText(TextFormat("%dx%d at %d, %d", selectionSize_.x,selectionSize_.y,selected_.column, selected_.row), 78, height - 70, 16, RAYWHITE);
    DrawText(inspect_ ? "Left click: inspect object" : "Left drag: paint", 78, height - 46, 14, muted);
    DrawText(inspect_ ? "Right click: erase object" : "Right drag: erase layer", 78, height - 27, 14, muted);
    if (inspect_) inspector_.draw(height);

    if (dropdown_) {
        const int rows = std::min(6, tilesets_.count());
        for (int i = 0; i < rows; ++i) {
            const int id = dropdownOffset_ + i;
            const Rectangle item{16, static_cast<float>(84 + i * 32), 328, 32};
            DrawRectangleRec(item, id == selected_.tileset ? Color{32, 81, 81, 255} : surface);
            BeginScissorMode(24, static_cast<int>(item.y), 310, 32);
            DrawText(tilesets_.get(id)->name.c_str(), 26, static_cast<int>(item.y + 8), 16, RAYWHITE);
            EndScissorMode();
        }
        DrawRectangleLinesEx({16, 84, 328, static_cast<float>(rows * 32)}, 1, accent);
    }
    if(customTypesOpen_ && grid_) {
        const auto types = grid_->objectTypes();
        const int count=static_cast<int>(types.size()), visible=std::min(5,count);
        auto found=types.begin(); std::advance(found,customTypeOffset_);
        for(int i=0;i<visible && found!=types.end();++i,++found) {
            BeginScissorMode(16,274+i*30,328,30);
            button({16,274.0f+i*30,328,30},found->second.displayName.c_str(),found->first==objectTypeId_);
            EndScissorMode();
        }
    }
    drawMenu();drawTransport();
}
