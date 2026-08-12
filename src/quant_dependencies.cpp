#include "quant_trading/quant_dependencies.h"

#include <mutex>
#include <unordered_map>

namespace quant_trading {
namespace {

class PaperExecutionVenue final : public IExecutionVenue {
public:
    ExecutionResult execute(const Order& order, double slippageBps) override {
        if (order.quantity <= 0.0 || order.referencePrice <= 0.0) {
            return {ExecutionResult::Status::Rejected, {}, "invalid paper order"};
        }
        const double direction = order.side == Side::Buy ? 1.0 : -1.0;
        Fill fill;
        fill.orderId = order.orderId;
        fill.side = order.side;
        fill.quantity = order.quantity;
        fill.price = order.referencePrice * (1.0 + direction * slippageBps / 10000.0);
        return {ExecutionResult::Status::Filled, std::move(fill), {}};
    }
};

class InMemoryTradingStore final : public ITradingStore {
public:
    bool persist(const StrategyId& strategyId,
                 const PortfolioSnapshot& portfolio,
                 std::string&) override {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_[strategyId] = portfolio;
        return true;
    }

private:
    std::mutex mutex_;
    std::unordered_map<StrategyId, PortfolioSnapshot> snapshots_;
};

}  // namespace

std::shared_ptr<IExecutionVenue> makePaperExecutionVenue() {
    return std::make_shared<PaperExecutionVenue>();
}

std::shared_ptr<ITradingStore> makeInMemoryTradingStore() {
    return std::make_shared<InMemoryTradingStore>();
}

}  // namespace quant_trading
