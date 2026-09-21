#pragma once
#include "SceneWorld.h"
#include <array>
struct EditorInput;
class HarvestEditor {
public:
    void reset() { loaded_=false;focus_=-1; }
    const std::string &selectedDefinition() const { return selected_; }
    bool capturesKeyboard() const { return focus_>=0; }
    bool update(SceneWorld &,const EditorInput &,int height,TileRef brush);
    void draw(const SceneWorld &,int height) const;
private:
    bool loaded_=false,remove_=false;
    int focus_=-1,action_=0,offset_=0;
    std::uint32_t species_=0;
    std::string selected_,message_;
    TileRef depletedTile_;
    std::array<std::string,7> fields_{};
    std::vector<std::array<std::string,3>> yields_;
    void select(const SceneWorld &,const std::string &);
};
