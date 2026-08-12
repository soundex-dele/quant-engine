#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "core/event.h"

namespace quant_trading {

using StrategyId = std::string;

enum class Side { Buy, Sell };
enum class Signal { Hold, Buy, Sell };
enum class StrategyState { Active, Paused, Cancelled, Failed, Exhausted, Stopped };
enum class QuantErrorCode {
    None,
    InvalidConfiguration,
    InvalidTick,
    RiskRejected,
    ExecutionRejected,
    DependencyFailure,
    Cancelled,
    Stopped,
    Internal
};

struct MarketTick {
    std::string symbol;
    std::int64_t timestampMs = 0;
    double price = 0.0;
    double quantity = 0.0;
};

struct Order {
    std::string orderId;
    std::string symbol;
    Side side = Side::Buy;
    double quantity = 0.0;
    double referencePrice = 0.0;
    std::uint64_t generation = 0;
};

struct Fill {
    std::string orderId;
    Side side = Side::Buy;
    double quantity = 0.0;
    double price = 0.0;
};

struct PortfolioSnapshot {
    std::string symbol;
    double cash = 0.0;
    double position = 0.0;
    double lastPrice = 0.0;
    double marketValue = 0.0;
    double equity = 0.0;
    std::uint64_t completedTicks = 0;
};

struct StrategyCreated { StrategyState state = StrategyState::Active; };
struct StrategyStateChanged { StrategyState state = StrategyState::Active; };
struct TickAccepted { std::uint64_t generation = 0; MarketTick tick; };
struct SignalGenerated {
    std::uint64_t generation = 0;
    Signal signal = Signal::Hold;
    double shortAverage = 0.0;
    double longAverage = 0.0;
};
struct RiskRejected { std::uint64_t generation = 0; std::string reason; };
struct OrderCreated { std::uint64_t generation = 0; Order order; };
struct FillReceived { std::uint64_t generation = 0; Fill fill; };
struct PortfolioUpdated { std::uint64_t generation = 0; PortfolioSnapshot portfolio; };
struct TickCompleted {
    std::uint64_t generation = 0;
    Signal signal = Signal::Hold;
    bool traded = false;
    bool exhausted = false;
    std::string detail;
    PortfolioSnapshot portfolio;
};
struct StrategyFailed {
    std::uint64_t generation = 0;
    QuantErrorCode code = QuantErrorCode::Internal;
    std::string message;
};

using QuantEvent = std::variant<StrategyCreated, StrategyStateChanged,
                                TickAccepted, SignalGenerated, RiskRejected,
                                OrderCreated, FillReceived, PortfolioUpdated,
                                TickCompleted, StrategyFailed>;
using QuantEventHandler = std::function<void(const QuantEvent&)>;

struct QuantEngineEvent {
    StrategyId strategyId;
    QuantEvent event;
};
using QuantEngineEventHandler = std::function<void(const QuantEngineEvent&)>;

struct QuantSubscription {
    std::function<void()> unsubscribe;
};

struct FlowPayload {
    QuantErrorCode errorCode = QuantErrorCode::None;
    std::string message;
};

struct QuantFlowToken : graphflow::core::EventT<QuantFlowToken> {
    std::uint64_t generation = 0;
    std::shared_ptr<FlowPayload> payload;
    QuantFlowToken() = default;
    explicit QuantFlowToken(std::uint64_t value) : generation(value) {}
    QuantFlowToken(std::uint64_t value, std::shared_ptr<FlowPayload> data)
        : generation(value), payload(std::move(data)) {}
};

enum class TickSubmitStatus { Accepted, Busy, Paused, Cancelled, Exhausted, NotFound, Stopped };
struct TickSubmitResult {
    TickSubmitStatus status = TickSubmitStatus::Stopped;
    std::uint64_t generation = 0;
};

enum class ControlAction { Pause, Resume, Cancel };
enum class ControlStatus { Accepted, AlreadyApplied, InvalidState, NotFound, Stopped };
struct ControlResult { ControlStatus status = ControlStatus::InvalidState; };

struct MarketTickRequested : graphflow::core::EventT<MarketTickRequested> {
    MarketTick tick;
    std::shared_ptr<TickSubmitResult> response;
    MarketTickRequested() = default;
    MarketTickRequested(MarketTick value, std::shared_ptr<TickSubmitResult> result)
        : tick(std::move(value)), response(std::move(result)) {}
};

struct StrategyControlRequested : graphflow::core::EventT<StrategyControlRequested> {
    ControlAction action = ControlAction::Pause;
    std::shared_ptr<ControlResult> response;
    StrategyControlRequested() = default;
    StrategyControlRequested(ControlAction value, std::shared_ptr<ControlResult> result)
        : action(value), response(std::move(result)) {}
};

struct WorkflowPublicEvent : graphflow::core::EventT<WorkflowPublicEvent> {
    QuantEvent event;
    WorkflowPublicEvent() = default;
    explicit WorkflowPublicEvent(QuantEvent value) : event(std::move(value)) {}
};

struct QuantTopics {
    static constexpr const char* MarketTick = "quant.market_tick";
    static constexpr const char* Control = "quant.control";
    static constexpr const char* PublicEvent = "quant.public_event";
};

}  // namespace quant_trading
