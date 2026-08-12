#include <string>

#include "quant_trading/api/quant_trading_api.h"

int main() {
    using namespace quant_trading::api;
    QuantTradingApi api;
    int eventCount = 0;
    auto subscription = api.subscribe([&eventCount](const ApiEvent&) { ++eventCount; });
    const std::string graph = std::string(GRAPHFLOW_SOURCE_DIR) +
        "/examples/quant_trading_workflow.json";
    const std::string config =
        "{\"symbol\":\"SMOKE\",\"workflow_path\":\"" + graph +
        "\",\"short_window\":2,\"long_window\":3,\"max_ticks\":1}";
    const auto created = api.createStrategy(config);
    if (created.status != ApiCreateStatus::Accepted) return 1;
    const auto tick = api.submitTick(created.strategyId, {"SMOKE", 1, 10.0, 1.0});
    if (tick.status != ApiTickStatus::Accepted || eventCount == 0) return 2;
    api.stop();
    if (subscription.unsubscribe) subscription.unsubscribe();
    return 0;
}
