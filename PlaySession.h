#pragma once
#include "SceneWorld.h"
#include <cmath>
#include <memory>
#include <stdexcept>

// No window dependency: callers supply elapsed frame time.
class FixedStep {
  public:
    int advance(SceneWorld &world, double elapsed) {
        if (!std::isfinite(elapsed) || elapsed <= 0)
            return 0;
        accumulator_ += elapsed;
        int ticks = 0;
        while (accumulator_ + 1e-12 >= interval && ticks < 5) {
            world.tick();
            accumulator_ -= interval;
            ++ticks;
        }
        if (ticks == 5 && accumulator_ + 1e-12 >= interval)
            accumulator_ = 0;
        return ticks;
    }
    void reset() {
        accumulator_ = 0;
    }
    static constexpr double interval = 1.0 / 60.0;

  private:
    double accumulator_ = 0;
};
class PlaySession {
  public:
    bool active() const {
        return static_cast<bool>(runtime_);
    }
    bool paused() const {
        return paused_;
    }
    void start(const SceneWorld &author) {
        if (active())
            return;
        auto candidate = std::make_unique<SceneWorld>(SceneWorld::fromDocument(author.document()));
        candidate->setHistoryEnabled(true);
        runtime_ = std::move(candidate);
        paused_ = false;
        stepper_.reset();
    }
    void stop(SceneWorld &author) {
        if (!active())
            return;
        author.reserveHistory(runtime_->guidState().used);
        runtime_.reset();
        paused_ = false;
        stepper_.reset();
    }
    void pause(bool paused) {
        if (active()) {
            paused_ = paused;
            stepper_.reset();
        }
    }
    void step() {
        if (active() && paused_)
            runtime_->tick();
    }
    int advance(double elapsed) {
        return active() && !paused_ ? stepper_.advance(*runtime_, elapsed) : 0;
    }
    SceneWorld &world() {
        if (!runtime_)
            throw std::logic_error("Play is stopped.");
        return *runtime_;
    }

  private:
    std::unique_ptr<SceneWorld> runtime_;
    bool paused_ = false;
    FixedStep stepper_;
};
