#include "DecisionBridge.h"
#include "NodeConfig.h"
#include <chrono>
#include <thread>
#include <iostream>
int main(int argc,char **argv) {
    try {
        if(argc!=2) throw std::runtime_error("Proxy URL required");
        ai::BridgeTransport bridge(JevNodeExecutable,JevDecisionBridge,JevProjectDirectory,argv[1],2);
        for (int i=0;i<2;++i) {
            ai::Json request{{"version",1},{"session","bridge-host"},{"request",std::to_string(i)},{"revision",i},
                {"self",ai::Json::object()},{"rules",{{"wait","Wait"}}},{"range",{{"radius",8}}},
                {"candidates",ai::Json::array({{{"id","c0"},{"action","wait"},{"parameters",{{"seconds",1}}}}})}};
            if(!bridge.submit(request)) throw std::runtime_error("Submit failed");
            if(bridge.submit(request)) throw std::runtime_error("Allowed concurrent submission");
            const auto start=std::chrono::steady_clock::now();
            for(;;) {
                if(auto result=bridge.poll()) {
                    if(result->at("request")!=std::to_string(i) || result->at("candidate")!="c0") throw std::runtime_error("Invalid bridge response");
                    break;
                }
                if(std::chrono::steady_clock::now()-start>std::chrono::seconds(5)) throw std::runtime_error("Bridge timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        bridge.cancel(); std::cout<<"C++ bridge passed\n"; return 0;
    }catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
