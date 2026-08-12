#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace quant_trading {

struct QuantConfig {
    std::string symbol;
    std::string workflowPath;
    std::size_t shortWindow = 3;
    std::size_t longWindow = 5;
    double orderQuantity = 1.0;
    double startingCash = 100000.0;
    double maxPosition = 100.0;
    double maxOrderNotional = 100000.0;
    double slippageBps = 0.0;
    std::uint64_t maxTicks = 1000;
    bool allowShort = false;

    static QuantConfig fromJson(const nlohmann::json& json);
    void validate() const;
};

}  // namespace quant_trading
