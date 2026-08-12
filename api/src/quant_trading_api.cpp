#include "quant_trading/api/quant_trading_api.h"

#include <stdexcept>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include "quant_trading/quant_engine.h"

namespace quant_trading::api {
namespace {

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

ApiSide side(Side value) { return value == Side::Buy ? ApiSide::Buy : ApiSide::Sell; }
ApiSignal signal(Signal value) {
    switch (value) {
        case Signal::Buy: return ApiSignal::Buy;
        case Signal::Sell: return ApiSignal::Sell;
        case Signal::Hold: return ApiSignal::Hold;
    }
    return ApiSignal::Hold;
}
ApiStrategyState state(StrategyState value) {
    switch (value) {
        case StrategyState::Active: return ApiStrategyState::Active;
        case StrategyState::Paused: return ApiStrategyState::Paused;
        case StrategyState::Cancelled: return ApiStrategyState::Cancelled;
        case StrategyState::Failed: return ApiStrategyState::Failed;
        case StrategyState::Exhausted: return ApiStrategyState::Exhausted;
        case StrategyState::Stopped: return ApiStrategyState::Stopped;
    }
    return ApiStrategyState::Stopped;
}
ApiErrorCode error(QuantErrorCode value) {
    switch (value) {
        case QuantErrorCode::None: return ApiErrorCode::None;
        case QuantErrorCode::InvalidConfiguration: return ApiErrorCode::Configuration;
        case QuantErrorCode::InvalidTick: return ApiErrorCode::InvalidTick;
        case QuantErrorCode::RiskRejected: return ApiErrorCode::RiskRejected;
        case QuantErrorCode::ExecutionRejected: return ApiErrorCode::ExecutionRejected;
        case QuantErrorCode::DependencyFailure: return ApiErrorCode::Dependency;
        case QuantErrorCode::Cancelled: return ApiErrorCode::Cancelled;
        case QuantErrorCode::Stopped: return ApiErrorCode::Stopped;
        case QuantErrorCode::Internal: return ApiErrorCode::Internal;
    }
    return ApiErrorCode::Internal;
}
ApiPortfolio portfolio(const PortfolioSnapshot& value) {
    return {value.symbol, value.cash, value.position, value.lastPrice,
            value.marketValue, value.equity, value.completedTicks};
}

ApiEvent flatten(const QuantEngineEvent& wrapped) {
    ApiEvent out;
    out.strategyId = wrapped.strategyId;
    std::visit(Overloaded{
        [&](const StrategyCreated& value) { out.type = ApiEventType::Created; out.state = state(value.state); },
        [&](const StrategyStateChanged& value) { out.type = ApiEventType::StateChanged; out.state = state(value.state); },
        [&](const TickAccepted& value) { out.type = ApiEventType::TickAccepted; out.generation = value.generation; out.price = value.tick.price; out.quantity = value.tick.quantity; },
        [&](const SignalGenerated& value) { out.type = ApiEventType::Signal; out.generation = value.generation; out.signal = signal(value.signal); out.shortAverage = value.shortAverage; out.longAverage = value.longAverage; },
        [&](const RiskRejected& value) { out.type = ApiEventType::RiskRejected; out.generation = value.generation; out.message = value.reason; },
        [&](const OrderCreated& value) { out.type = ApiEventType::OrderCreated; out.generation = value.generation; out.orderId = value.order.orderId; out.side = side(value.order.side); out.quantity = value.order.quantity; out.price = value.order.referencePrice; },
        [&](const FillReceived& value) { out.type = ApiEventType::Fill; out.generation = value.generation; out.orderId = value.fill.orderId; out.side = side(value.fill.side); out.quantity = value.fill.quantity; out.price = value.fill.price; },
        [&](const PortfolioUpdated& value) { out.type = ApiEventType::Portfolio; out.generation = value.generation; out.portfolio = portfolio(value.portfolio); },
        [&](const TickCompleted& value) { out.type = ApiEventType::TickCompleted; out.generation = value.generation; out.signal = signal(value.signal); out.traded = value.traded; out.exhausted = value.exhausted; out.message = value.detail; out.portfolio = portfolio(value.portfolio); },
        [&](const StrategyFailed& value) { out.type = ApiEventType::Failed; out.generation = value.generation; out.errorCode = error(value.code); out.message = value.message; }
    }, wrapped.event);
    return out;
}

ApiTickStatus tickStatus(TickSubmitStatus value) {
    switch (value) {
        case TickSubmitStatus::Accepted: return ApiTickStatus::Accepted;
        case TickSubmitStatus::Busy: return ApiTickStatus::Busy;
        case TickSubmitStatus::Paused: return ApiTickStatus::Paused;
        case TickSubmitStatus::Cancelled: return ApiTickStatus::Cancelled;
        case TickSubmitStatus::Exhausted: return ApiTickStatus::Exhausted;
        case TickSubmitStatus::NotFound: return ApiTickStatus::NotFound;
        case TickSubmitStatus::Stopped: return ApiTickStatus::Stopped;
    }
    return ApiTickStatus::Stopped;
}
ApiControlStatus controlStatus(ControlStatus value) {
    switch (value) {
        case ControlStatus::Accepted: return ApiControlStatus::Accepted;
        case ControlStatus::AlreadyApplied: return ApiControlStatus::AlreadyApplied;
        case ControlStatus::InvalidState: return ApiControlStatus::InvalidState;
        case ControlStatus::NotFound: return ApiControlStatus::NotFound;
        case ControlStatus::Stopped: return ApiControlStatus::Stopped;
    }
    return ApiControlStatus::Stopped;
}

}  // namespace

class QuantTradingApi::Impl {
public:
    Impl() : engine(QuantDependencies{makePaperExecutionVenue(),
                                      makeInMemoryTradingStore()}) {}
    QuantTradingEngine engine;
    bool stopped = false;
};

QuantTradingApi::QuantTradingApi() : impl_(std::make_unique<Impl>()) {}
QuantTradingApi::~QuantTradingApi() noexcept { stop(); }
QuantTradingApi::QuantTradingApi(QuantTradingApi&&) noexcept = default;
QuantTradingApi& QuantTradingApi::operator=(QuantTradingApi&&) noexcept = default;

ApiCreateResult QuantTradingApi::createStrategy(const std::string& configJson) {
    if (impl_->stopped) return {ApiCreateStatus::Stopped, {}, "API stopped"};
    try {
        auto config = QuantConfig::fromJson(nlohmann::json::parse(configJson));
        return {ApiCreateStatus::Accepted, impl_->engine.createStrategy(std::move(config)), {}};
    } catch (const nlohmann::json::exception& e) {
        return {ApiCreateStatus::ConfigurationError, {}, e.what()};
    } catch (const std::invalid_argument& e) {
        return {ApiCreateStatus::ConfigurationError, {}, e.what()};
    } catch (const std::exception& e) {
        return {ApiCreateStatus::WorkflowError, {}, e.what()};
    }
}

ApiDestroyStatus QuantTradingApi::destroyStrategy(const std::string& id) {
    switch (impl_->engine.destroyStrategy(id)) {
        case DestroyStatus::Accepted: return ApiDestroyStatus::Accepted;
        case DestroyStatus::NotFound: return ApiDestroyStatus::NotFound;
        case DestroyStatus::Stopped: return ApiDestroyStatus::Stopped;
    }
    return ApiDestroyStatus::Stopped;
}

ApiTickResult QuantTradingApi::submitTick(const std::string& id,
                                          const ApiMarketTick& tick) {
    auto result = impl_->engine.submitTick(id, MarketTick{tick.symbol, tick.timestampMs,
                                                          tick.price, tick.quantity});
    return {tickStatus(result.status), result.generation};
}
ApiControlResult QuantTradingApi::pause(const std::string& id) { return {controlStatus(impl_->engine.pause(id).status)}; }
ApiControlResult QuantTradingApi::resume(const std::string& id) { return {controlStatus(impl_->engine.resume(id).status)}; }
ApiControlResult QuantTradingApi::cancel(const std::string& id) { return {controlStatus(impl_->engine.cancel(id).status)}; }

ApiSubscription QuantTradingApi::subscribe(ApiEventHandler handler) {
    auto subscription = impl_->engine.subscribe(
        [callback = std::move(handler)](const QuantEngineEvent& event) mutable {
            try { callback(flatten(event)); } catch (...) {}
        });
    return ApiSubscription{[value = std::move(subscription)]() mutable {
        if (value.unsubscribe) value.unsubscribe();
    }};
}

void QuantTradingApi::stop() noexcept {
    if (!impl_ || impl_->stopped) return;
    impl_->stopped = true;
    impl_->engine.stop();
}

}  // namespace quant_trading::api
