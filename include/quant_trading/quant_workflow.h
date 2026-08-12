#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "quant_trading/quant_config.h"
#include "quant_trading/quant_dependencies.h"
#include "quant_trading/quant_types.h"

namespace graphflow::core { class EventBus; }
namespace graphflow::engine { class GraphEngine; }

namespace quant_trading {

class QuantRuntimeContext;

class QuantTradingWorkflow {
public:
    QuantTradingWorkflow(StrategyId strategyId, QuantConfig config,
                         QuantDependencies dependencies);
    ~QuantTradingWorkflow();

    bool initialize();
    bool start();
    void stop() noexcept;

    TickSubmitResult submitTick(MarketTick tick);
    ControlResult pause();
    ControlResult resume();
    ControlResult cancel();
    QuantSubscription subscribe(QuantEventHandler handler);
    StrategyState state() const;
    PortfolioSnapshot portfolio() const;

private:
    struct Subscribers;
    ControlResult control(ControlAction action);
    void dispatch(const QuantEvent& event);

    StrategyId strategyId_;
    QuantConfig config_;
    QuantDependencies dependencies_;
    std::shared_ptr<QuantRuntimeContext> context_;
    std::unique_ptr<graphflow::engine::GraphEngine> engine_;
    graphflow::core::EventBus* eventBus_ = nullptr;
    std::shared_ptr<Subscribers> subscribers_;
    mutable std::recursive_mutex mutex_;
    bool initialized_ = false;
    bool started_ = false;
};

}  // namespace quant_trading
