#include "Guid.h"
#include <stdexcept>

Guid Guid::fromString(const std::string &text) {
    if (text.size()!=36) throw std::invalid_argument("Invalid GUID text.");
    Guid id;std::size_t byte=0;
    const auto hex=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::invalid_argument("Invalid GUID digit.");};
    for(std::size_t i=0;i<text.size();) {
        if(i==8 || i==13 || i==18 || i==23) {if(text[i++]!='-')throw std::invalid_argument("Invalid GUID separator.");continue;}
        id.bytes[byte++]=static_cast<std::uint8_t>((hex(text[i])<<4)|hex(text[i+1]));i+=2;
    }
    if(id.empty()) throw std::invalid_argument("Empty GUID.");
    return id;
}
#include <random>
#include <stdexcept>

Guid GuidRegistry::generate(const std::function<Guid()>& candidate) {
    std::lock_guard lock(mutex_);
    for(int attempt=0;attempt<1024;++attempt) {
        const auto id=candidate();
        if(!id.empty() && used_.insert(id).second) return id;
    }
    throw std::runtime_error("Unable to generate an unused GUID.");
}
bool GuidRegistry::reserve(Guid id) {
    if(id.empty()) throw std::invalid_argument("Cannot reserve an empty GUID.");
    std::lock_guard lock(mutex_);
    return used_.insert(id).second;
}
bool GuidRegistry::hasUsed(Guid id) const {
    std::lock_guard lock(mutex_);
    return used_.contains(id);
}
GuidRegistry& GlobalGuidRegistry() { static GuidRegistry registry; return registry; }
Guid GenerateUniqueGuid() {
    return GlobalGuidRegistry().generate([] {
        static std::random_device random;
        Guid result;
        for(auto& byte:result.bytes) byte=static_cast<std::uint8_t>(random());
        result.bytes[6]=(result.bytes[6]&15)|64;
        result.bytes[8]=(result.bytes[8]&63)|128;
        return result;
    });
}
Guid Guid::generate() { return GenerateUniqueGuid(); }
std::string Guid::toString() const {
    constexpr char hex[]="0123456789abcdef";
    std::string result;
    for(std::size_t i=0;i<bytes.size();++i) {
        if(i==4 || i==6 || i==8 || i==10) result+='-';
        result+=hex[bytes[i]>>4];result+=hex[bytes[i]&15];
    }
    return result;
}
