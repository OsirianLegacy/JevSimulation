#include "Entity.h"
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>

struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

int main() {
    try {
        std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
        scene::Resource empty;
        if (std::string(scene::Resource::rulesDescription("stamina")) !=
            "Pool reserved for stamina. No action costs are configured.")
            throw std::runtime_error("Missing stamina rules");
        std::cout << empty.fetchData() << '\n';
        Entity creature;
        creature.adjustResource("health", -12.5f);
        creature.setResource("mana", scene::ResourcePool(0));
        const auto before = creature;
        std::cout << creature.resources().fetchData() << '\n';
        if (creature != before) throw std::runtime_error("Fetch changed entity state");
        creature.adjustResource("health", -7.5f);
        std::cout << creature.resources().fetchData() << '\n';
        scene::Resource special;
        special.set("quote\"slash\\\n\t\r\b\f\x01", scene::ResourcePool(12.5f, 0.125f));
        special.set("caf\xc3\xa9\xe6\xb0\xb4\xf0\x9f\x90\xba", scene::ResourcePool(1));
        special.set("extreme", scene::ResourcePool(std::numeric_limits<float>::max(),
                                                  std::numeric_limits<float>::denorm_min()));
        std::cout << special.fetchData() << '\n';
        for (const auto &key : {std::string("\xff"), std::string("\xc0\xaf"),
                               std::string("\xe0\x80\x80"), std::string("\xed\xa0\x80"),
                               std::string("\xf4\x90\x80\x80"), std::string("\xc3"),
                               std::string("\xc3x")}) {
            scene::Resource invalid;
            invalid.set(key, scene::ResourcePool{});
            bool rejected = false;
            try { invalid.fetchData(); }
            catch (const std::invalid_argument &) { rejected = true; }
            if (!rejected) throw std::runtime_error("Invalid UTF-8 was serialized");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
