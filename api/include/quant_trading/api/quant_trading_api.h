#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace quant_trading::api {

enum class ApiSide { Buy, Sell };
enum class ApiSignal { Hold, Buy, Sell };
enum class ApiStrategyState { Active, Paused, Cancelled, Failed, Exhausted, Stopped };
enum class ApiEventType {
    Created, StateChanged, TickAccepted, Signal, RiskRejected,
    OrderCreated, Fill, Portfolio, TickCompleted, Failed
};
enum class ApiErrorCode {
    None, Configuration, InvalidTick, RiskRejected, ExecutionRejected,
    Dependency, Cancelled, Stopped, Internal
};

struct ApiMarketTick {
    std::string symbol;
    std::int64_t timestampMs = 0;
    double price = 0.0;
    double quantity = 0.0;
};

struct ApiPortfolio {
    std::string symbol;
    double cash = 0.0;
    double position = 0.0;
    double lastPrice = 0.0;
    double marketValue = 0.0;
    double equity = 0.0;
    std::uint64_t completedTicks = 0;
};

struct ApiEvent {
    ApiEventType type = ApiEventType::Created;
    std::string strategyId;
    std::uint64_t generation = 0;
    ApiStrategyState state = ApiStrategyState::Active;
    ApiSignal signal = ApiSignal::Hold;
    ApiSide side = ApiSide::Buy;
    ApiErrorCode errorCode = ApiErrorCode::None;
    std::string message;
    std::string orderId;
    double quantity = 0.0;
    double price = 0.0;
    double shortAverage = 0.0;
    double longAverage = 0.0;
    bool traded = false;
    bool exhausted = false;
    ApiPortfolio portfolio;
};

enum class ApiCreateStatus { Accepted, ConfigurationError, WorkflowError, Stopped };
struct ApiCreateResult {
    ApiCreateStatus status = ApiCreateStatus::WorkflowError;
    std::string strategyId;
    std::string errorMessage;
};

enum class ApiDestroyStatus { Accepted, NotFound, Stopped };
enum class ApiTickStatus { Accepted, Busy, Paused, Cancelled, Exhausted, NotFound, Stopped };
struct ApiTickResult { ApiTickStatus status = ApiTickStatus::Stopped; std::uint64_t generation = 0; };
enum class ApiControlStatus { Accepted, AlreadyApplied, InvalidState, NotFound, Stopped };
struct ApiControlResult { ApiControlStatus status = ApiControlStatus::Stopped; };

using ApiEventHandler = std::function<void(const ApiEvent&)>;
struct ApiSubscription { std::function<void()> unsubscribe; };

class QuantTradingApi {
public:
    QuantTradingApi();
    ~QuantTradingApi() noexcept;
    QuantTradingApi(QuantTradingApi&&) noexcept;
    QuantTradingApi& operator=(QuantTradingApi&&) noexcept;
    QuantTradingApi(const QuantTradingApi&) = delete;
    QuantTradingApi& operator=(const QuantTradingApi&) = delete;

    // Operations are synchronous. submitTick returns after one complete graph
    // traversal (including execution and persistence). Event handlers run on
    // the calling thread and must return promptly; handler exceptions are
    // contained by the facade.
    ApiCreateResult createStrategy(const std::string& configJson);
    ApiDestroyStatus destroyStrategy(const std::string& strategyId);
    ApiTickResult submitTick(const std::string& strategyId, const ApiMarketTick& tick);
    ApiControlResult pause(const std::string& strategyId);
    ApiControlResult resume(const std::string& strategyId);
    ApiControlResult cancel(const std::string& strategyId);
    ApiSubscription subscribe(ApiEventHandler handler);
    // cancel, destroyStrategy, stop, and subscription unsubscription are safe
    // to call repeatedly; subsequent operations receive explicit terminal
    // status values.
    void stop() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace quant_trading::api
