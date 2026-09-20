#include "Entity.h"
#include <iostream>
#include <stdexcept>

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try { action(); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("Invalid entity operation accepted");
}
int main() {
    try {
        Entity creature;
        Entity wolf("wolf", scene::ResourcePool(80, 60));
        check(creature.type() == "creature" && creature.health().current() == 100 &&
                  creature.health().maximum() == 100, "Default health");
        check(wolf.type() == "wolf" && wolf.health().current() == 60, "Custom type and health");
        check(!wolf.id().empty() && wolf.id() != creature.id() &&
                  GlobalGuidRegistry().hasUsed(wolf.id()), "Shared unique GUIDs");
        auto snapshot = wolf;
        check(wolf.adjustResource("health", -100) == -60 && wolf.health().depleted(),
              "Damage clamps at zero");
        check(snapshot.id() == wolf.id() && snapshot.health().current() == 60,
              "Snapshots preserve identity and independent state");
        check(wolf.adjustResource("health", 200) == 80 && wolf.health().full(),
              "Healing clamps at maximum");
        rejects([&] { wolf.removeResource("health"); });
        check(wolf.health().full(), "Rejected removal preserves health");
        wolf.setResource("stamina", scene::ResourcePool(20));
        check(!wolf.trySpendResource("stamina", 21) &&
                  wolf.trySpendResource("stamina", 5) && wolf.health().current() == 80,
              "Optional pools are independent");
        check(wolf.removeResource("stamina") && !wolf.removeResource("stamina"), "Optional removal");
        wolf.setResource("health", scene::ResourcePool(40, 10));
        check(wolf.health().maximum() == 40 && wolf.health().current() == 10, "Health replacement");
        rejects([] { Entity invalid(""); });
        rejects([] { Entity invalid(" \t\r\n"); });
        rejects([] { Entity invalid(std::string(256, 'x')); });
        rejects([] { Entity invalid(std::string("wolf\0hidden", 11)); });
        std::cout << "Entity tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
