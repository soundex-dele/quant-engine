#include "quant_trading/quant_config.h"

#include <cmath>
#include <stdexcept>

namespace quant_trading {

QuantConfig QuantConfig::fromJson(const nlohmann::json& json) {
    if (!json.is_object()) throw std::invalid_argument("quant config must be an object");
    QuantConfig config;
    config.symbol = json.value("symbol", std::string{});
    config.workflowPath = json.value("workflow_path", std::string{});
    config.shortWindow = json.value("short_window", config.shortWindow);
    config.longWindow = json.value("long_window", config.longWindow);
    config.orderQuantity = json.value("order_quantity", config.orderQuantity);
    config.startingCash = json.value("starting_cash", config.startingCash);
    config.maxPosition = json.value("max_position", config.maxPosition);
    config.maxOrderNotional = json.value("max_order_notional", config.maxOrderNotional);
    config.slippageBps = json.value("slippage_bps", config.slippageBps);
    config.maxTicks = json.value("max_ticks", config.maxTicks);
    config.allowShort = json.value("allow_short", config.allowShort);
    config.validate();
    return config;
}

void QuantConfig::validate() const {
    if (symbol.empty()) throw std::invalid_argument("symbol must not be empty");
    if (workflowPath.empty()) throw std::invalid_argument("workflow_path must not be empty");
    if (shortWindow == 0 || longWindow == 0 || shortWindow >= longWindow) {
        throw std::invalid_argument("require 0 < short_window < long_window");
    }
    if (!std::isfinite(orderQuantity) || orderQuantity <= 0.0) {
        throw std::invalid_argument("order_quantity must be positive");
    }
    if (!std::isfinite(startingCash) || startingCash < 0.0 ||
        !std::isfinite(maxPosition) || maxPosition <= 0.0 ||
        !std::isfinite(maxOrderNotional) || maxOrderNotional <= 0.0) {
        throw std::invalid_argument("cash and risk limits are invalid");
    }
    if (!std::isfinite(slippageBps) || slippageBps < 0.0 || slippageBps >= 10000.0) {
        throw std::invalid_argument("slippage_bps must be in [0, 10000)");
    }
    if (maxTicks == 0) throw std::invalid_argument("max_ticks must be positive");
}

}  // namespace quant_trading
