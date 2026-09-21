#pragma once
#include "Decision.h"

struct PlayerInput {
    Vector2 mouse{};
    bool leftPressed=false, leftDown=false, leftReleased=false, rightPressed=false;
    bool shift=false, cancel=false, focused=true;
    bool worldInput=true, keyboardInput=true;
};

// Transient RTS selection and command routing; no input polling or saved state.
class PlayerController {
public:
    void clear();
    void update(SceneWorld &,ai::DecisionSystem &,const Camera2D &,const PlayerInput &,float frameSeconds);
    const std::vector<Guid> &selection() const { return selected_; }
    bool dragging() const { return selecting_ && dragged_; }
    const std::string &status() const { return status_; }
    void drawWorld(const SceneWorld &,const Camera2D &,const ai::DecisionSystem &) const;
    void drawScreen(const Camera2D &) const;
private:
    std::vector<Guid> selected_;
    bool selecting_=false,dragged_=false,additive_=false;
    Vector2 startScreen_{},startWorld_{},endWorld_{};
    std::string status_;
    Vector2 commandPoint_{};
    float markerSeconds_=0;
    bool commandAccepted_=false;
    void prune(const SceneWorld &);
    void finishSelection(const SceneWorld &,const Camera2D &,Vector2 mouse);
    void command(SceneWorld &,ai::DecisionSystem &,CellPosition target);
};
