#include "quant_trading/runtime/quant_runtime_context.h"

#include <utility>

namespace quant_trading {

QuantRuntimeContext::QuantRuntimeContext(StrategyId id, QuantConfig value,
                                         QuantDependencies deps)
    : strategyId(std::move(id)), config(std::move(value)),
      dependencies(std::move(deps)), cash(config.startingCash) {}

bool QuantRuntimeContext::isCurrent(std::uint64_t value) const noexcept {
    return generation == value && !isTerminal();
}

bool QuantRuntimeContext::isTerminal() const noexcept {
    return state == StrategyState::Cancelled || state == StrategyState::Failed ||
           state == StrategyState::Exhausted || state == StrategyState::Stopped;
}

PortfolioSnapshot QuantRuntimeContext::portfolioSnapshot() const {
    PortfolioSnapshot snapshot;
    snapshot.symbol = config.symbol;
    snapshot.cash = cash;
    snapshot.position = position;
    snapshot.lastPrice = lastPrice;
    snapshot.marketValue = position * lastPrice;
    snapshot.equity = cash + snapshot.marketValue;
    snapshot.completedTicks = completedTicks;
    return snapshot;
}

}  // namespace quant_trading
