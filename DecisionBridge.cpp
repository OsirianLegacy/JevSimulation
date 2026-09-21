#include "DecisionBridge.h"
namespace ai {
BridgeTransport::BridgeTransport(std::wstring exe, std::wstring script, std::wstring directory, std::string url, double timeout, std::wstring managedConfig)
    : executable_(std::move(exe)),script_(std::move(script)),directory_(std::move(directory)),managedConfig_(std::move(managedConfig)),url_(std::move(url)),timeout_(timeout) {}
bool BridgeTransport::submit(const Json &request) {
    if (!process_) {
        std::vector<std::wstring> args{script_,std::wstring(url_.begin(),url_.end()),std::to_wstring(static_cast<int>(timeout_*1000))};
        if (!managedConfig_.empty()) args.push_back(managedConfig_);
        process_ = std::make_unique<NodeProcess>(executable_,args,directory_,true);
    }
    return process_->sendLine(request.dump());
}
std::optional<Json> BridgeTransport::poll() {
    if (!process_) return {};
    auto line=process_->readLine();
    if (line) return Json::parse(*line);
    if (process_->poll()) throw std::runtime_error("Decision bridge stopped.");
    return {};
}
void BridgeTransport::cancel() { process_.reset(); }
}
