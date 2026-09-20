#include "Entity.h"
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try { action(); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("Invalid position accepted");
}
struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};
int main() {
    try {
        std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
        Entity creature;
        check(creature.position() == scene::Position{}, "Every entity has a default position");
        std::cout << creature.position().fetchData() << '\n';
        creature.position().setWorldPosition({15.5f, 8});
        check(creature.position().cellCoordinates() == CellPosition{1, 1}, "Cell boundary conversion");
        const Entity snapshot = creature;
        std::cout << snapshot.position().fetchData() << '\n';
        creature.position().setCellCoordinates({2, 3});
        check(creature.position().worldPosition().x == 16 &&
              creature.position().worldPosition().y == 24, "Cell updates synchronize world position");
        check(snapshot.position().worldPosition().x == 15.5f, "Snapshot position is independent");
        creature.position().setWorldPosition({-0.25f, -8.25f});
        check(creature.position().cellCoordinates() == CellPosition{-1, -2}, "Negative coordinates use floor");
        std::cout << creature.position().fetchData() << '\n';
        Grid grid(4, 4, 4, {10, -20});
        Entity wolf("wolf", scene::ResourcePool(80), scene::Position({15.5f, -12}, 4, {10, -20}));
        check(wolf.position().cellCoordinates() == grid.worldToCell({15.5f, -12}).value(),
              "Custom grid conversion matches Grid");
        std::cout << wolf.position().fetchData() << '\n';
        const auto before = wolf;
        rejects([&] { wolf.position().setWorldPosition({11, std::numeric_limits<float>::infinity()}); });
        rejects([&] { wolf.position().setWorldPosition({std::numeric_limits<float>::max(), 0}); });
        rejects([&] { wolf.position().setCellCoordinates({std::numeric_limits<int>::max(), 0}); });
        check(wolf == before, "Failed updates preserve both coordinates");
        rejects([] { scene::Position invalid({}, 0); });
        rejects([] { scene::Position invalid({}, -8); });
        rejects([] { scene::Position invalid({}, std::numeric_limits<float>::quiet_NaN()); });
        rejects([] { scene::Position invalid({}, 8, {0, std::numeric_limits<float>::infinity()}); });
        check(snapshot.position().fetchData() == snapshot.position().fetchData(), "Stable JSON");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
