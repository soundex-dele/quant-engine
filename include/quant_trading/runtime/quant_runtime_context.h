#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include "core/node_runtime_context.h"
#include "quant_trading/quant_config.h"
#include "quant_trading/quant_dependencies.h"
#include "quant_trading/quant_types.h"

namespace quant_trading {

struct TickCycle {
    MarketTick tick;
    Signal signal = Signal::Hold;
    double shortAverage = 0.0;
    double longAverage = 0.0;
    double approvedQuantity = 0.0;
    std::optional<Order> order;
    std::optional<Fill> fill;
    std::string detail;
    bool traded = false;
    bool limitOnly = false;
};

class QuantRuntimeContext final : public graphflow::core::NodeRuntimeContext {
public:
    QuantRuntimeContext(StrategyId strategyId, QuantConfig config,
                        QuantDependencies dependencies);

    bool isCurrent(std::uint64_t generation) const noexcept;
    bool isTerminal() const noexcept;
    PortfolioSnapshot portfolioSnapshot() const;

    StrategyId strategyId;
    QuantConfig config;
    QuantDependencies dependencies;
    StrategyState state = StrategyState::Active;
    std::uint64_t generation = 0;
    std::uint64_t completedTicks = 0;
    bool busy = false;
    std::int64_t lastTimestampMs = -1;
    std::deque<double> prices;
    std::optional<double> previousSpread;
    TickCycle cycle;
    double cash = 0.0;
    double position = 0.0;
    double lastPrice = 0.0;
};

}  // namespace quant_trading
