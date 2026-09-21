#pragma once
#include "SceneWorld.h"
#include "Range.h"
#include "third_party/nlohmann/json.hpp"
#include <array>
#include <deque>
#include <functional>
#include <random>

namespace ai {
using Json = nlohmann::json;
struct Band { int maxDistance; double interval; int weight; };
struct Config {
    int version = 1, batchSize = 32, perceptionRadius = 8, candidateCount = 4, rerouteAttempts = 2;
    int payloadBytes = 8192;
    double requestsPerSecond = 10, timeout = 2, queueWait = 2, backoffMin = 1, backoffMax = 30;
    double moveStepSeconds = 0.25, waitSeconds = 1;
    unsigned seed = 12345;
    bool trace = false, managedProxy = true;
    std::string provider = "proxy", proxyUrl = "http://127.0.0.1:8787/decision";
    std::array<Band,5> bands{{{0,1,8},{1,2,4},{3,5,2},{7,15,1},{2147483647,60,1}}};
    static Config load(const std::string &path);
    void validate() const;
};
struct GoalRecord {
    std::string id, action, source = "fallback", status = "pending";
    Guid entity;
    Json parameters = Json::object(), result = nullptr;
    Json json() const;
};
struct Execution {
    GoalRecord goal;
    double elapsed = 0;
    double workElapsed = 0;
    std::string harvestDefinition;
    int reroutes = 0;
    std::vector<CellPosition> path;
    std::size_t next = 1;
};
struct ActionDefinition {
    std::string rules;
    std::function<std::vector<Json>(const SceneWorld &, Guid, const Config &, std::mt19937 &)> candidates;
    std::function<bool(const SceneWorld &, Guid, const Json &)> validate;
    std::function<void(SceneWorld &, Execution &, double, const Config &)> execute;
};
class ActionRegistry {
  public:
    ActionRegistry();
    void add(std::string name, ActionDefinition definition);
    const ActionDefinition &at(const std::string &name) const;
    const auto &definitions() const { return definitions_; }
    void tick(SceneWorld &world, Execution &execution, double dt, const Config &config) const;
  private:
    std::map<std::string, ActionDefinition> definitions_;
};
class DecisionContextBuilder {
  public:
    using Contributor = std::function<Json(const SceneWorld &, Guid)>;
    DecisionContextBuilder();
    void add(std::string name, std::string rules, Contributor contributor);
    Json build(const SceneWorld &world, Guid entity, const Json &previous,
               const std::string &session, const std::string &request, std::uint64_t revision,
               const Config &config, const ActionRegistry &actions, std::mt19937 &rng) const;
  private:
    struct Entry { std::string rules; Contributor fetch; };
    std::map<std::string, Entry> contributors_;
};
class Transport {
  public:
    virtual ~Transport() = default;
    virtual bool submit(const Json &request) = 0;
    virtual std::optional<Json> poll() = 0;
    virtual void cancel() = 0;
};
class FakeTransport : public Transport {
  public:
    bool submit(const Json &request) override;
    std::optional<Json> poll() override;
    void cancel() override { pending_.reset(); }
  private:
    std::optional<Json> pending_;
};
struct Metrics {
    std::uint64_t requests = 0, bytes = 0, stale = 0, failures = 0, fallback = 0, succeeded = 0, cancelled = 0;
    double latencySeconds = 0, queueSeconds = 0;
    Json json() const;
};
class DecisionSystem {
  public:
    DecisionSystem(SceneWorld &world, Config config, std::unique_ptr<Transport> transport = {});
    ~DecisionSystem();
    void update(double simulationDelta, double wallSeconds, bool paused = false);
    void step(double simulationDelta); // Execute one paused step without network dispatch.
    void invalidate();
    bool assign(Guid entity, const std::string &action, Json parameters, std::string source = "fallback");
    void cancel(Guid entity, const std::string &reason = "interrupted");
    std::optional<GoalRecord> current(Guid entity) const;
    Json lastOutcome(Guid entity) const;
    Json metrics() const;
    const std::string &session() const { return session_; }
    std::size_t queuedCount() const;
    bool inFlight() const { return flight_.has_value(); }
    int bandFor(Guid entity) const;
    std::size_t lastBatchSize() const { return lastBatch_; }
    ActionRegistry &actions() { return actions_; }
    DecisionContextBuilder &context() { return builder_; }
  private:
    struct State {
        std::optional<Execution> execution;
        Json previous = nullptr;
        double ready = 0, queuedAt = 0;
        bool queued = false;
        int band = 4;
        std::uint64_t revision = 0;
    };
    struct Flight { Guid entity; Json request; double sent; int band; };
    SceneWorld &world_;
    Config config_;
    std::unique_ptr<Transport> transport_;
    ActionRegistry actions_;
    DecisionContextBuilder builder_;
    std::mt19937 rng_;
    std::string session_;
    std::uint64_t serial_ = 0, membership_ = UINT64_MAX;
    std::unordered_map<Guid, State, GuidHash> states_;
    std::vector<Guid> roster_, players_;
    std::array<std::deque<std::pair<Guid, std::uint64_t>>,5> queues_;
    std::vector<int> serviceOrder_;
    std::size_t cursor_ = 0, serviceCursor_ = 0, lastBatch_ = 0;
    std::optional<Flight> flight_;
    std::array<Metrics,5> metrics_;
    double time_ = 0, nextDispatch_ = 0, backoffUntil_ = 0, backoff_ = 1;
    bool paused_ = false;
    void sync();
    void finish(Guid id, State &state);
    void fallback(Guid id, State &state);
    Json snapshot(Guid id, State &state);
    void remoteFailure(double wall);
};
}
