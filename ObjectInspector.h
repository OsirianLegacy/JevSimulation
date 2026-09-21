#pragma once
#include "SceneWorld.h"
#include <string>

struct EditorInput;
class ObjectInspector {
public:
    void select(const SceneWorld& grid, CellPosition cell);
    void clear();
    void refresh(const SceneWorld& world);
    void update(SceneWorld& grid, const EditorInput& input, int height, bool readOnly=false);
    void draw(int height) const;
    bool capturesKeyboard() const { return focus_ >= 0; }
    Guid selected() const { return selected_; }
private:
    struct Row { std::string definition, name, quantity; Guid guid{}; };
    Guid selected_{}, detail_{};
    bool detailsMode_=false;
    bool readOnly_=false, canOpen_=false, canContain_=false;
    ObjectType type_ = ObjectType::Container;
    bool open_ = false;
    std::vector<Row> rows_;
    int focus_ = -1;
    float scroll_ = 0;
    std::string error_;
    const SceneWorld* grid_ = nullptr;
    std::size_t itemChoice_ = 0;
    std::string harvestId_;
    void resetDraft(const Object& object);
    void clampScroll(int height);
};
