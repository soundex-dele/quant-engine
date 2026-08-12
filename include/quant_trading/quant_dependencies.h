#pragma once

#include <memory>
#include <string>

#include "quant_trading/quant_types.h"

namespace quant_trading {

struct ExecutionResult {
    enum class Status { Filled, Rejected, Error };
    Status status = Status::Error;
    Fill fill;
    std::string message;
};

class IExecutionVenue {
public:
    virtual ~IExecutionVenue() = default;
    virtual ExecutionResult execute(const Order& order, double slippageBps) = 0;
};

class ITradingStore {
public:
    virtual ~ITradingStore() = default;
    virtual bool persist(const StrategyId& strategyId,
                         const PortfolioSnapshot& portfolio,
                         std::string& error) = 0;
};

struct QuantDependencies {
    std::shared_ptr<IExecutionVenue> executionVenue;
    std::shared_ptr<ITradingStore> store;
};

std::shared_ptr<IExecutionVenue> makePaperExecutionVenue();
std::shared_ptr<ITradingStore> makeInMemoryTradingStore();

}  // namespace quant_trading
