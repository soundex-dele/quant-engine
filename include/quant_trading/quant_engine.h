#pragma once

#include <memory>
#include <string>

#include "quant_trading/quant_config.h"
#include "quant_trading/quant_dependencies.h"
#include "quant_trading/quant_types.h"

namespace quant_trading {

enum class DestroyStatus { Accepted, NotFound, Stopped };

class QuantTradingEngine {
public:
    explicit QuantTradingEngine(QuantDependencies dependencies);
    ~QuantTradingEngine();

    StrategyId createStrategy(QuantConfig config);
    DestroyStatus destroyStrategy(const StrategyId& strategyId);
    TickSubmitResult submitTick(const StrategyId& strategyId, MarketTick tick);
    ControlResult pause(const StrategyId& strategyId);
    ControlResult resume(const StrategyId& strategyId);
    ControlResult cancel(const StrategyId& strategyId);
    QuantSubscription subscribe(QuantEngineEventHandler handler);
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace quant_trading
