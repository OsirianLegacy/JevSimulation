#pragma once
#include <string>
#include <array>
#include <random>
#include <unordered_set>
#include <stdexcept>
namespace scene {
struct HumanName {
    std::string first, last;
    std::string full() const { return first+" "+last; }
    bool operator==(const HumanName &) const = default;
    void validate() const {
        for(const auto *part:{&first,&last}) {
            if(part->empty() || part->size()>100 || part->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789")!=std::string::npos)
                throw std::invalid_argument("Invalid human name.");
        }
    }
};
inline HumanName generateHumanName(const std::unordered_set<std::string> &reserved) {
    static constexpr std::array first{"Ada","Adrian","Alex","Amara","Aria","Arthur","Avery","Caleb",
        "Clara","Dara","Elias","Elise","Emilia","Ethan","Felix","Finn","Hana","Hazel","Iris","Isaac",
        "Jasper","Jonah","Kai","Leah","Leo","Lina","Maya","Milo","Nora","Owen","Rowan","Theo"};
    static constexpr std::array last{"Ashford","Bennett","Birch","Brooks","Carter","Clarke","Collins","Dawson",
        "Ellis","Everett","Finch","Fletcher","Foster","Gray","Green","Hale","Hart","Hayes","Hill","Lane",
        "Marsh","Miller","Morgan","Parker","Reed","Rivera","Rowe","Shaw","Stone","Turner","Ward","Wells"};
    static thread_local std::mt19937 random(std::random_device{}());
    constexpr auto count=first.size()*last.size();
    const auto start=std::uniform_int_distribution<std::size_t>(0,count-1)(random);
    // Visit every combination before using a compound family-name variant. Never retry forever.
    for(std::size_t i=0;i<count;++i) {
        const auto n=(start+i)%count;HumanName name{first[n/last.size()],last[n%last.size()]};
        if(!reserved.contains(name.full()))return name;
    }
    HumanName name{first[start/last.size()],last[start%last.size()]};
    const auto base=name.last;
    for(std::size_t n=0;;++n) {
        name.last=base;
        auto suffix=n;
        do { name.last+="-"+std::string(last[suffix%last.size()]);suffix/=last.size(); } while(suffix);
        if(!reserved.contains(name.full()))return name;
    }
}
}
