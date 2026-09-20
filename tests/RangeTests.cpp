#include "Range.h"
#include "Entity.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try { action(); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("Negative range accepted");
}
int main() {
    try {
        scene::Range zero;
        check(zero.contains({3, 4}, {3, 4}) && !zero.contains({3, 4}, {3, 5}), "Zero is center only");
        check(zero.cells({3, 4}, {0, 0, 6, 6}) == std::vector<CellPosition>{{3, 4}}, "Zero enumeration");
        scene::Range range(1);
        for (const auto target : {CellPosition{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
            check(range.contains({}, target), "Cardinal neighbors cost one");
        for (const auto target : {CellPosition{1, 1}, {-1, 1}, {1, -1}, {-1, -1}})
            check(!range.contains({}, target) && scene::Range::distance({}, target) == 2,
                  "Diagonal neighbors cost two");
        range.setRadius(2);
        check(range.contains({}, {1, 1}) && range.contains({}, {2, 0}) &&
              !range.contains({}, {2, 1}), "Inclusive diamond boundary");
        const auto diamond = range.cells({}, {-3, -3, 4, 4});
        check(diamond.size() == 13, "Radius two covers thirteen cells");
        check(range.cells({}, {0, 0, 4, 4}).size() == 6, "Clips at map edge");
        check(range.cells({}, {0, 0, 0, 4}).empty(), "Empty bounds");
        check(range.cells({-1, 0}, {0, 0, 4, 4}).size() == 3, "Outside center can overlap bounds");
        for (int radius = 0; radius < 6; ++radius) {
            scene::Range sample(radius);
            std::vector<CellPosition> expected;
            for (int y = -4; y < 7; ++y)
                for (int x = -5; x < 6; ++x)
                    if (std::abs(x - 1) + std::abs(y + 2) <= radius) expected.push_back({x, y});
            check(sample.cells({1, -2}, {-5, -4, 6, 7}) == expected, "Enumeration and ordering");
        }
        constexpr int low = std::numeric_limits<int>::min(), high = std::numeric_limits<int>::max();
        check(scene::Range::distance({low, low}, {high, high}) == 8589934590LL, "Distance cannot overflow");
        scene::Range huge(high);
        check(!huge.contains({low, low}, {high, high}), "Extreme distance remains out of range");
        check(huge.cells({high, high}, {high - 2, high - 2, high, high}).size() == 4,
              "Clipped enumeration handles extreme centers");
        rejects([] { scene::Range invalid(-1); });
        rejects([&] { range.setRadius(-1); });
        check(range.radius() == 2, "Invalid edit preserves radius");
        Entity creature;
        creature.position().setCellCoordinates({4, -2});
        const auto before = creature;
        std::cout << range.fetchData(creature.position().cellCoordinates()) << '\n';
        check(creature == before && range.radius() == 2, "Export preserves state");
        creature.position().setCellCoordinates({5, -1});
        std::cout << range.fetchData(creature.position().cellCoordinates()) << '\n';
        std::cout << zero.fetchData({}) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
