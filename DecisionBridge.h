#pragma once
#include "Decision.h"
#include "NodeProcess.h"
namespace ai {
class BridgeTransport : public Transport {
  public:
    BridgeTransport(std::wstring executable, std::wstring script, std::wstring directory,
                    std::string url, double timeout);
    bool submit(const Json &request) override;
    std::optional<Json> poll() override;
    void cancel() override;
  private:
    std::wstring executable_, script_, directory_;
    std::string url_;
    double timeout_;
    std::unique_ptr<NodeProcess> process_;
};
}
