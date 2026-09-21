#include "Decision.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <climits>

namespace ai {
namespace {
Json cellJson(CellPosition c) { return {{"x",c.x},{"y",c.y}}; }
CellPosition cell(const Json &j) {
    if (!j.is_object() || !j.contains("x") || !j.contains("y") ||
        !j.at("x").is_number_integer() || !j.at("y").is_number_integer())
        throw std::invalid_argument("Cell requires integer coordinates.");
    const auto x = j.at("x").get<std::int64_t>(), y = j.at("y").get<std::int64_t>();
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) throw std::invalid_argument("Cell overflow.");
    return {static_cast<int>(x),static_cast<int>(y)};
}
void complete(Execution &e, const std::string &status, const std::string &reason) {
    e.goal.status = status; e.goal.result = {{"reason",reason}};
}
}
void Config::validate() const {
    const auto positive = [](double x) { return std::isfinite(x) && x > 0; };
    if (version != 1 || batchSize < 1 || batchSize > 4096 || perceptionRadius < 1 || perceptionRadius > 128 ||
        candidateCount < 1 || candidateCount > 4 || payloadBytes < 2048 || payloadBytes > 8192 ||
        rerouteAttempts < 0 || rerouteAttempts > 20 || !positive(requestsPerSecond) || requestsPerSecond > 100 ||
        !positive(timeout) || timeout > 30 || !positive(queueWait) || !positive(backoffMin) ||
        !positive(backoffMax) || backoffMax < backoffMin || !positive(moveStepSeconds) ||
        !positive(waitSeconds) || waitSeconds > 3600 ||
        (provider != "fake" && provider != "proxy" && provider != "fallback-only"))
        throw std::invalid_argument("Invalid AI configuration.");
    int previous = -1;
    for (const auto &b : bands) {
        if (b.maxDistance <= previous || !positive(b.interval) || b.weight < 1 || b.weight > 100)
            throw std::invalid_argument("Invalid AI distance bands.");
        previous = b.maxDistance;
    }
    if (bands.back().maxDistance != INT_MAX || proxyUrl.rfind("http://127.0.0.1:",0) != 0)
        throw std::invalid_argument("AI requires a final catch-all band and a loopback proxy.");
}
Config Config::load(const std::string &path) {
    std::ifstream input(path); if (!input) throw std::runtime_error("Cannot open AI config: " + path);
    auto j = Json::parse(input); Config c;
    const auto read = [&]<class T>(const char *key, T &value) {
        if (!j.contains(key)) return;
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T,bool>) {
            if (!j.at(key).is_number_integer() || j.at(key).get<double>() < 0 ||
                j.at(key).get<double>() > std::numeric_limits<T>::max()) throw std::invalid_argument("Invalid integer configuration.");
        }
        j.at(key).get_to(value);
    };
#define AI_FIELD(name) read(#name,c.name)
    AI_FIELD(version); AI_FIELD(provider); AI_FIELD(proxyUrl); AI_FIELD(seed); AI_FIELD(batchSize);
    AI_FIELD(perceptionRadius); AI_FIELD(candidateCount); AI_FIELD(rerouteAttempts); AI_FIELD(payloadBytes);
    AI_FIELD(requestsPerSecond); AI_FIELD(timeout); AI_FIELD(queueWait); AI_FIELD(backoffMin); AI_FIELD(backoffMax);
    AI_FIELD(moveStepSeconds); AI_FIELD(waitSeconds); AI_FIELD(trace);
#undef AI_FIELD
    if (j.contains("bands")) {
        if (!j.at("bands").is_array() || j.at("bands").size() != 5) throw std::invalid_argument("Expected five AI bands.");
        for (int i=0;i<5;++i) c.bands[i] = {j["bands"][i].at("maxDistance").get<int>(),
            j["bands"][i].at("interval").get<double>(),j["bands"][i].at("weight").get<int>()};
    }
    c.validate(); return c;
}
Json GoalRecord::json() const {
    return {{"id",id},{"entity",entity.toString()},{"action",action},{"parameters",parameters},
            {"source",source},{"status",status},{"result",result}};
}
ActionRegistry::ActionRegistry() {
    add("move", {
        "Move to the destination cell using a four-way path. Blocked steps are rerouted; unrecoverable paths fail.",
        [](const SceneWorld &world, Guid id, const Config &c, std::mt19937 &rng) {
            auto reachable = world.reachableCells(id,c.perceptionRadius);
            if (!reachable.empty()) reachable.erase(reachable.begin());
            std::shuffle(reachable.begin(),reachable.end(),rng);
            std::vector<Json> result;
            for (const auto &[target,steps] : reachable) {
                if (result.size() >= static_cast<std::size_t>(c.candidateCount - 1)) break;
                result.push_back({{"action","move"},{"parameters",{{"destination",cellJson(target)}}},{"pathSteps",steps}});
            }
            return result;
        },
        [](const SceneWorld &world, Guid id, const Json &p) {
            try { return world.findCreature(id).has_value() && !world.creaturePath(id,cell(p.at("destination"))).empty(); }
            catch (const std::exception &) { return false; }
        },
        [](SceneWorld &world, Execution &e, double dt, const Config &c) {
            const auto actor = world.findCreature(e.goal.entity);
            if (!actor) { complete(e,"cancelled","entity_removed"); return; }
            const auto target = cell(e.goal.parameters.at("destination"));
            if (actor->position().cellCoordinates() == target) { complete(e,"succeeded","arrived"); return; }
            e.elapsed += dt;
            if (e.elapsed < c.moveStepSeconds) return;
            e.elapsed -= c.moveStepSeconds;
            if (e.path.empty()) { e.path = world.creaturePath(e.goal.entity,target); e.next = 1; }
            if (e.path.size() <= e.next || e.path[e.next-1] != actor->position().cellCoordinates() ||
                !world.moveCreature(e.goal.entity,e.path[e.next])) {
                e.path.clear();
                if (e.reroutes++ >= c.rerouteAttempts) {
                    complete(e,"failed","unreachable"); e.goal.result["destination"] = cellJson(target);
                }
                return;
            }
            if (++e.next == e.path.size()) complete(e,"succeeded","arrived");
        }
    });
    add("wait", {
        "Remain in the current cell for the specified duration in simulation seconds.",
        [](const SceneWorld &, Guid, const Config &c, std::mt19937 &) {
            return std::vector<Json>{{{"action","wait"},{"parameters",{{"seconds",c.waitSeconds}}}}};
        },
        [](const SceneWorld &world, Guid id, const Json &p) {
            try { const double t = p.at("seconds").get<double>();
                return world.findCreature(id).has_value() && std::isfinite(t) && t > 0 && t <= 3600;
            } catch (const std::exception &) { return false; }
        },
        [](SceneWorld &world, Execution &e, double dt, const Config &) {
            if (!world.findCreature(e.goal.entity)) { complete(e,"cancelled","entity_removed"); return; }
            e.elapsed += dt;
            if (e.elapsed >= e.goal.parameters.at("seconds").get<double>()) complete(e,"succeeded","wait_completed");
        }
    });
}
void ActionRegistry::add(std::string name, ActionDefinition def) {
    if (name.empty() || def.rules.empty() || !def.candidates || !def.validate || !def.execute || definitions_.contains(name))
        throw std::invalid_argument("Invalid or duplicate action definition.");
    definitions_.emplace(std::move(name),std::move(def));
}
const ActionDefinition &ActionRegistry::at(const std::string &name) const { return definitions_.at(name); }
void ActionRegistry::tick(SceneWorld &world, Execution &e, double dt, const Config &config) const {
    if (e.goal.status == "pending") e.goal.status = "running";
    if (e.goal.status == "running") at(e.goal.action).execute(world,e,dt,config);
}
DecisionContextBuilder::DecisionContextBuilder() {
    add("health",scene::Resource::HealthRulesDescription,[](const SceneWorld &w, Guid id) {
        const auto h = w.resource(id,scene::Resource::Health).value();
        return Json{{"current",h.current()},{"maximum",h.maximum()}};
    });
    add("position",scene::Position::RulesDescription,[](const SceneWorld &w, Guid id) {
        const auto p = w.findCreature(id)->position(); const auto wp = p.worldPosition();
        return Json{{"cell",cellJson(p.cellCoordinates())},{"world",{{"x",wp.x},{"y",wp.y}}},
                    {"cellSize",p.cellSize()},{"origin",{{"x",p.gridOrigin().x},{"y",p.gridOrigin().y}}}};
    });
}
void DecisionContextBuilder::add(std::string name, std::string rules, Contributor fetch) {
    if (name.empty() || rules.empty() || !fetch || contributors_.contains(name)) throw std::invalid_argument("Invalid contributor.");
    contributors_.emplace(std::move(name),Entry{std::move(rules),std::move(fetch)});
}
Json DecisionContextBuilder::build(const SceneWorld &world, Guid id, const Json &previous,
                                  const std::string &session, const std::string &request, std::uint64_t revision,
                                  const Config &c, const ActionRegistry &actions, std::mt19937 &rng) const {
    if (!world.findCreature(id)) throw std::invalid_argument("Missing decision entity.");
    Json result{{"version",1},{"session",session},{"request",request},{"revision",revision},
        {"self",Json::object()},{"rules",Json::object()},{"previousGoal",previous},
        {"range",{{"radius",c.perceptionRadius},{"center",cellJson(world.findCreature(id)->position().cellCoordinates())}}},
        {"candidates",Json::array()}};
    result["rules"]["range"] = scene::Range::RulesDescription;
    result["rules"]["resources"] = scene::Resource::RulesDescription;
    for (const auto &[name, entry] : contributors_) {
        result["self"][name] = entry.fetch(world,id); result["rules"][name] = entry.rules;
    }
    for (const auto &[name, action] : actions.definitions()) {
        result["rules"][name] = action.rules;
        if (name == "wait") continue;
        for (auto candidate : action.candidates(world,id,c,rng)) {
            if (result["candidates"].size() >= static_cast<std::size_t>(c.candidateCount-1)) break;
            if (previous.is_object() && previous.value("status","") == "failed" &&
                previous.value("action","") == name && previous.at("parameters") == candidate.at("parameters")) continue;
            result["candidates"].push_back(std::move(candidate));
        }
    }
    result["candidates"].push_back(actions.at("wait").candidates(world,id,c,rng).at(0));
    for (std::size_t i=0;i<result["candidates"].size();++i) result["candidates"][i]["id"] = "c" + std::to_string(i);
    while (result.dump().size() > static_cast<std::size_t>(c.payloadBytes) && result["candidates"].size() > 1)
        result["candidates"].erase(result["candidates"].begin());
    if (result.dump().size() > static_cast<std::size_t>(c.payloadBytes)) throw std::length_error("Required decision context exceeds payload limit.");
    return result;
}
bool FakeTransport::submit(const Json &r) {
    if (pending_) return false;
    pending_ = {{"version",1},{"session",r.at("session")},{"request",r.at("request")},{"revision",r.at("revision")},
                {"candidate",r.at("candidates").at(0).at("id")}};
    return true;
}
std::optional<Json> FakeTransport::poll() { auto result = std::move(pending_); pending_.reset(); return result; }
Json Metrics::json() const {
    return {{"requests",requests},{"requestBytes",bytes},{"staleResponses",stale},{"failures",failures},
        {"fallback",fallback},{"succeeded",succeeded},{"cancelled",cancelled},
        {"latencySeconds",latencySeconds},{"queueSeconds",queueSeconds}};
}
DecisionSystem::DecisionSystem(SceneWorld &w, Config c, std::unique_ptr<Transport> t)
    : world_(w),config_(std::move(c)),transport_(std::move(t)),rng_(config_.seed),session_(GenerateUniqueGuid().toString()),backoff_(config_.backoffMin) {
    config_.validate();
    if (!transport_ && config_.provider == "fake") transport_ = std::make_unique<FakeTransport>();
    // Interleave weighted slots rather than sending all of a band's quota together.
    for (int n=0;n<100;++n) for (int b=0;b<5;++b) if (n < config_.bands[b].weight) serviceOrder_.push_back(b);
    sync();
}
DecisionSystem::~DecisionSystem() {
    if (transport_) transport_->cancel();
    for (auto &[id,s] : states_) if (s.execution) {
        complete(*s.execution,"cancelled","session_stopped"); finish(id,s);
    }
}
void DecisionSystem::sync() {
    if (membership_ == world_.creatureMembershipRevision()) return;
    membership_ = world_.creatureMembershipRevision();
    auto all = world_.creatureIds();
    std::sort(all.begin(),all.end(),[](Guid a, Guid b) { return a.bytes < b.bytes; });
    roster_.clear(); players_.clear();
    std::unordered_set<Guid,GuidHash> present;
    for (auto id : all) {
        if (world_.findCreature(id)->control() == ControlOwnership::Player) players_.push_back(id);
        else { present.insert(id); roster_.push_back(id); states_.try_emplace(id); }
    }
    for (auto it=states_.begin();it!=states_.end();) {
        if (!present.contains(it->first)) {
            if (it->second.execution) { complete(*it->second.execution,"cancelled","entity_removed"); finish(it->first,it->second); }
            if (flight_ && flight_->entity == it->first) { if (transport_) transport_->cancel(); flight_.reset(); }
            it = states_.erase(it);
        } else ++it;
    }
    cursor_ = 0;
}
int DecisionSystem::bandFor(Guid id) const {
    auto e = world_.findCreature(id); if (!e || players_.empty()) return 4;
    auto chunk = world_.chunkOf(e->position().cellCoordinates());
    std::int64_t best = INT_MAX;
    for (auto player : players_) if (auto p = world_.findCreature(player))
        best = std::min(best,scene::Range::distance(chunk,world_.chunkOf(p->position().cellCoordinates())));
    for (int i=0;i<5;++i) if (best <= config_.bands[i].maxDistance) return i;
    return 4;
}
void DecisionSystem::finish(Guid, State &s) {
    auto &m = metrics_[s.band];
    if (s.execution->goal.status == "succeeded") ++m.succeeded;
    else if (s.execution->goal.status == "cancelled") ++m.cancelled;
    else ++m.failures;
    s.previous = s.execution->goal.json();
    if (config_.trace) std::clog << Json{{"event","goal_result"},{"goal",s.previous}}.dump() << '\n';
    s.execution.reset();
}
bool DecisionSystem::assign(Guid id, const std::string &name, Json p, std::string source) {
    auto it = states_.find(id); if (it == states_.end()) return false;
    try { if (!actions_.at(name).validate(world_,id,p)) return false; }
    catch (const std::exception &) { return false; }
    auto &s = it->second;
    if (s.execution) cancel(id);
    s.queued = false; ++s.revision;
    Execution e; e.goal = {session_ + ":g" + std::to_string(++serial_),name,std::move(source),"pending",id,std::move(p),nullptr};
    if (config_.trace) std::clog << Json{{"event","goal_start"},{"goal",e.goal.json()}}.dump() << '\n';
    s.execution = std::move(e); return true;
}
void DecisionSystem::cancel(Guid id, const std::string &reason) {
    auto it = states_.find(id); if (it == states_.end()) return;
    auto &s = it->second; s.queued = false; ++s.revision;
    if (flight_ && flight_->entity == id) { if (transport_) transport_->cancel(); flight_.reset(); }
    if (s.execution) { complete(*s.execution,"cancelled",reason); finish(id,s); }
}
Json DecisionSystem::snapshot(Guid id, State &s) {
    return builder_.build(world_,id,s.previous,session_,session_+":r"+std::to_string(++serial_),s.revision,config_,actions_,rng_);
}
void DecisionSystem::fallback(Guid id, State &s) {
    s.queued = false;
    try {
        auto candidates = snapshot(id,s).at("candidates");
        std::vector<Json> moves;
        for (const auto &c : candidates) if (c.at("action") != "wait") moves.push_back(c);
        const auto choice = moves.empty() ? candidates.back() : moves[std::uniform_int_distribution<std::size_t>(0,moves.size()-1)(rng_)];
        if (assign(id,choice.at("action"),choice.at("parameters"))) { ++metrics_[s.band].fallback; return; }
    } catch (const std::exception &) {}
    if (assign(id,"wait",{{"seconds",config_.waitSeconds}})) ++metrics_[s.band].fallback;
}
void DecisionSystem::remoteFailure(double wall) {
    if (transport_) transport_->cancel();
    if (flight_) {
        auto it = states_.find(flight_->entity);
        ++metrics_[flight_->band].failures;
        if (it != states_.end() && !it->second.execution) fallback(it->first,it->second);
    }
    flight_.reset(); backoffUntil_ = wall + backoff_; backoff_ = std::min(backoff_*2,config_.backoffMax);
}
void DecisionSystem::update(double dt, double wall, bool paused) {
    if (!std::isfinite(dt) || dt < 0 || !std::isfinite(wall) || wall < 0) throw std::invalid_argument("Invalid AI clock.");
    lastBatch_ = 0;
    if (paused) {
        if (!paused_ && flight_) {
            if (transport_) transport_->cancel();
            auto it = states_.find(flight_->entity); if (it != states_.end()) { ++it->second.revision; it->second.ready = time_; }
            flight_.reset();
        }
        paused_ = true; return;
    }
    paused_ = false; time_ += dt; sync();
    for (auto &[id,s] : states_) if (s.execution) {
        actions_.tick(world_,*s.execution,dt,config_);
        if (s.execution->goal.status != "pending" && s.execution->goal.status != "running") finish(id,s);
    }
    if (flight_) {
        std::optional<Json> response;
        try { response = transport_->poll(); } catch (const std::exception &) { remoteFailure(wall); }
        if (flight_ && response) {
            auto &f = *flight_; auto it = states_.find(f.entity);
            bool accepted = false;
            try {
                if (response->at("version") == 1 && response->at("session") == session_ &&
                    response->at("request") == f.request.at("request") && response->at("revision") == f.request.at("revision") &&
                    world_.findCreature(f.entity) && cellJson(world_.findCreature(f.entity)->position().cellCoordinates()) == f.request.at("self").at("position").at("cell") &&
                    it != states_.end() && it->second.revision == f.request.at("revision") && !it->second.execution) {
                    for (const auto &candidate : f.request.at("candidates")) if (candidate.at("id") == response->at("candidate")) {
                        accepted = assign(f.entity,candidate.at("action"),candidate.at("parameters"),"jev"); break;
                    }
                }
            } catch (const std::exception &) {}
            metrics_[f.band].latencySeconds += std::max(0.0,wall-f.sent);
            if (accepted) { flight_.reset(); backoff_ = config_.backoffMin; }
            else { ++metrics_[f.band].stale; remoteFailure(wall); }
        } else if (flight_ && wall-flight_->sent >= config_.timeout) remoteFailure(wall);
    }
    const auto work = std::min<std::size_t>(config_.batchSize,roster_.size());
    for (std::size_t i=0;i<work;++i) {
        const auto id = roster_[cursor_++ % roster_.size()]; auto &s = states_.at(id); ++lastBatch_;
        if (s.execution || (flight_ && flight_->entity == id)) continue;
        s.band = bandFor(id);
        const bool available = transport_ && config_.provider != "fallback-only" && wall >= backoffUntil_;
        if (!available || time_ < s.ready || (s.queued && time_-s.queuedAt >= config_.queueWait)) {
            if (time_ >= s.ready) s.ready = time_ + config_.bands[s.band].interval;
            fallback(id,s); continue;
        }
        if (!s.queued) { s.queued=true; s.queuedAt=time_; ++s.revision; queues_[s.band].emplace_back(id,s.revision); }
    }
    // Retire obsolete queue tickets even while offline, bounding memory growth.
    for (auto &q : queues_) for (int i=0;i<config_.batchSize && !q.empty();++i) {
        auto it = states_.find(q.front().first);
        if (it != states_.end() && it->second.queued && it->second.revision == q.front().second) break;
        q.pop_front();
    }
    if (flight_ || !transport_ || config_.provider == "fallback-only" || wall < backoffUntil_ || wall < nextDispatch_) return;
    for (std::size_t n=0;n<serviceOrder_.size();++n) {
        int band = serviceOrder_[serviceCursor_++ % serviceOrder_.size()]; auto &q = queues_[band];
        if (q.empty()) continue;
        auto [id,ticket] = q.front(); q.pop_front(); auto it=states_.find(id);
        if (it == states_.end() || !it->second.queued || it->second.revision != ticket) continue;
        auto &s=it->second; const auto currentBand=bandFor(id);
        if (currentBand != band) { s.band=currentBand; queues_[currentBand].emplace_back(id,ticket); continue; }
        s.queued=false; s.ready=time_+config_.bands[band].interval;
        try {
            auto request=snapshot(id,s);
            flight_=Flight{id,request,wall,band};
            if (!transport_->submit(request)) { remoteFailure(wall); return; }
            if (config_.trace) std::clog << Json{{"event","request"},{"request",request.at("request")},{"entity",id.toString()},{"band",band}}.dump() << '\n';
            auto &m=metrics_[band]; ++m.requests; m.bytes+=request.dump().size(); m.queueSeconds+=time_-s.queuedAt;
            nextDispatch_=wall+1.0/config_.requestsPerSecond;
        } catch (const std::exception &) {
            if (flight_) remoteFailure(wall); else fallback(id,s);
        }
        break;
    }
}
void DecisionSystem::invalidate() {
    if (transport_) transport_->cancel(); flight_.reset();
    for (auto &[id,s] : states_) { if (s.execution) { complete(*s.execution,"cancelled","session_reset"); finish(id,s); } }
    states_.clear(); roster_.clear(); players_.clear(); for (auto &q:queues_) q.clear();
    session_=GenerateUniqueGuid().toString(); membership_=UINT64_MAX; time_=0; nextDispatch_=0; backoffUntil_=0; backoff_=config_.backoffMin; paused_=false; sync();
}
std::optional<GoalRecord> DecisionSystem::current(Guid id) const {
    auto it=states_.find(id); return it != states_.end() && it->second.execution ? std::optional(it->second.execution->goal) : std::nullopt;
}
Json DecisionSystem::lastOutcome(Guid id) const { auto it=states_.find(id); return it == states_.end() ? Json(nullptr) : it->second.previous; }
std::size_t DecisionSystem::queuedCount() const { std::size_t n=0; for (const auto &[id,s]:states_) n+=s.queued; return n; }
Json DecisionSystem::metrics() const { Json bands=Json::array(); for (const auto &m:metrics_) bands.push_back(m.json()); return {{"bands",bands},{"queued",queuedCount()},{"inFlight",inFlight()}}; }
}
