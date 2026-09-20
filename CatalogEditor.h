#pragma once
#include "SceneWorld.h"

struct EditorInput;
// Creation drafts are separate from saved catalog entries.
class CatalogEditor {
public:
    void reset();
    bool capturesKeyboard() const {return focus_>=0;}
    bool update(SceneWorld& grid, const EditorInput& input, bool objectTypes, int height);
    void draw(const SceneWorld& grid, bool objectTypes, int height) const;
private:
    std::string id_, name_, message_;
    ObjectType behavior_ = ObjectType::Container;
    int focus_ = -1, offset_ = 0;
    bool error_ = false;
};
