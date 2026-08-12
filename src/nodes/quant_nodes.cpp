#include "quant_trading/nodes/quant_nodes.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace quant_trading {

QuantNode::QuantNode(std::string name, graphflow::core::EventBus* bus,
                     graphflow::core::Blackboard* blackboard,
                     std::shared_ptr<QuantRuntimeContext> context,
                     nlohmann::json params)
    : GraphNode(name, bus, blackboard), context_(std::move(context)),
      params_(std::move(params)) {}

bool QuantNode::doInit() {
    if (!m_eventBus || !m_blackboard || !context_ || subscribed_) return false;
    subscribed_ = subscribeInputs();
    return subscribed_;
}

bool QuantNode::accept(const QuantFlowToken& token) const noexcept {
    return context_ && context_->isCurrent(token.generation);
}

void QuantNode::publishToken(const QuantFlowToken& token,
                             std::string_view condition) {
    publish(token, std::string(condition));
}

void QuantNode::fail(std::uint64_t generation, QuantErrorCode code,
                     std::string message) {
    auto payload = std::make_shared<FlowPayload>();
    payload->errorCode = code;
    payload->message = std::move(message);
    publishToken(QuantFlowToken{generation, std::move(payload)}, "error");
}

void QuantNode::publishPublic(QuantEvent event) {
    m_eventBus->publish(std::string(QuantTopics::PublicEvent),
                        WorkflowPublicEvent{std::move(event)});
}

bool StrategyControlNode::subscribeInputs() {
    m_eventBus->subscribeAll<StrategyControlRequested>(
        QuantTopics::Control, [this](StrategyControlRequested& request) {
            if (!request.response) return;
            auto& ctx = context();
            if (ctx.state == StrategyState::Stopped) {
                request.response->status = ControlStatus::Stopped;
                return;
            }
            if (request.action == ControlAction::Pause) {
                if (ctx.state == StrategyState::Paused) {
                    request.response->status = ControlStatus::AlreadyApplied;
                } else if (ctx.state == StrategyState::Active && !ctx.busy) {
                    ctx.state = StrategyState::Paused;
                    request.response->status = ControlStatus::Accepted;
                    publishPublic(StrategyStateChanged{ctx.state});
                }
                return;
            }
            if (request.action == ControlAction::Resume) {
                if (ctx.state == StrategyState::Active) {
                    request.response->status = ControlStatus::AlreadyApplied;
                } else if (ctx.state == StrategyState::Paused) {
                    ctx.state = StrategyState::Active;
                    request.response->status = ControlStatus::Accepted;
                    publishPublic(StrategyStateChanged{ctx.state});
                }
                return;
            }
            if (ctx.state == StrategyState::Cancelled) {
                request.response->status = ControlStatus::AlreadyApplied;
            } else if (!ctx.isTerminal()) {
                ctx.state = StrategyState::Cancelled;
                ctx.busy = false;
                request.response->status = ControlStatus::Accepted;
                publishPublic(StrategyStateChanged{ctx.state});
            }
        });
    return true;
}

bool MarketIngressNode::subscribeInputs() {
    m_eventBus->subscribeAll<MarketTickRequested>(
        QuantTopics::MarketTick, [this](MarketTickRequested& request) {
            if (!request.response) return;
            auto& ctx = context();
            request.response->generation = ctx.generation;
            if (ctx.state == StrategyState::Stopped) {
                request.response->status = TickSubmitStatus::Stopped;
                return;
            }
            if (ctx.state == StrategyState::Paused) {
                request.response->status = TickSubmitStatus::Paused;
                return;
            }
            if (ctx.state == StrategyState::Cancelled || ctx.state == StrategyState::Failed) {
                request.response->status = TickSubmitStatus::Cancelled;
                return;
            }
            if (ctx.state == StrategyState::Exhausted) {
                request.response->status = TickSubmitStatus::Exhausted;
                return;
            }
            if (ctx.busy) {
                request.response->status = TickSubmitStatus::Busy;
                return;
            }

            ctx.busy = true;
            const auto generation = ++ctx.generation;
            request.response->generation = generation;
            request.response->status = TickSubmitStatus::Accepted;
            ctx.cycle = TickCycle{};
            ctx.cycle.tick = request.tick;

            if (ctx.completedTicks >= ctx.config.maxTicks) {
                ctx.cycle.limitOnly = true;
                publishToken(QuantFlowToken{generation}, "limit");
                return;
            }
            const bool valid = request.tick.symbol == ctx.config.symbol &&
                               request.tick.timestampMs > ctx.lastTimestampMs &&
                               std::isfinite(request.tick.price) && request.tick.price > 0.0 &&
                               std::isfinite(request.tick.quantity) && request.tick.quantity > 0.0;
            if (!valid) {
                auto payload = std::make_shared<FlowPayload>();
                payload->errorCode = QuantErrorCode::InvalidTick;
                payload->message = "tick must match symbol, advance time, and contain positive finite values";
                publishToken(QuantFlowToken{generation, std::move(payload)}, "invalid");
                return;
            }
            ctx.lastTimestampMs = request.tick.timestampMs;
            ctx.lastPrice = request.tick.price;
            ctx.prices.push_back(request.tick.price);
            while (ctx.prices.size() > ctx.config.longWindow) ctx.prices.pop_front();
            publishPublic(TickAccepted{generation, request.tick});
            publishToken(QuantFlowToken{generation}, "valid");
        });
    return true;
}

bool IndicatorNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        if (!accept(token)) return;
        auto& ctx = context();
        if (ctx.prices.empty()) {
            fail(token.generation, QuantErrorCode::Internal, "price history is empty");
            return;
        }
        const auto shortCount = std::min(ctx.config.shortWindow, ctx.prices.size());
        const auto shortBegin = ctx.prices.end() - static_cast<std::ptrdiff_t>(shortCount);
        ctx.cycle.shortAverage = std::accumulate(shortBegin, ctx.prices.end(), 0.0) / shortCount;
        ctx.cycle.longAverage = std::accumulate(ctx.prices.begin(), ctx.prices.end(), 0.0) /
                                ctx.prices.size();
        publishToken(token, "ok");
    });
    return true;
}

bool SignalNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        if (!accept(token)) return;
        auto& ctx = context();
        const double epsilon = params().value("epsilon", 1e-9);
        Signal signal = Signal::Hold;
        if (ctx.prices.size() >= ctx.config.longWindow) {
            const double spread = ctx.cycle.shortAverage - ctx.cycle.longAverage;
            if (!ctx.previousSpread) {
                if (spread > epsilon) signal = Signal::Buy;
                else if (spread < -epsilon) signal = Signal::Sell;
            } else if (spread > epsilon && *ctx.previousSpread <= epsilon) {
                signal = Signal::Buy;
            } else if (spread < -epsilon && *ctx.previousSpread >= -epsilon) {
                signal = Signal::Sell;
            }
            ctx.previousSpread = spread;
        }
        ctx.cycle.signal = signal;
        publishPublic(SignalGenerated{token.generation, signal,
                                      ctx.cycle.shortAverage, ctx.cycle.longAverage});
        publishToken(token, signal == Signal::Buy ? "buy" :
                            signal == Signal::Sell ? "sell" : "hold");
    });
    return true;
}

bool RiskNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        if (!accept(token)) return;
        auto& ctx = context();
        double quantity = ctx.config.orderQuantity;
        std::string reason;
        if (quantity * ctx.lastPrice > ctx.config.maxOrderNotional) {
            reason = "maximum order notional exceeded";
        } else if (ctx.cycle.signal == Signal::Buy) {
            if (ctx.position + quantity > ctx.config.maxPosition) reason = "maximum position exceeded";
            else if (quantity * ctx.lastPrice > ctx.cash) reason = "insufficient cash";
        } else {
            if (!ctx.config.allowShort) quantity = std::min(quantity, ctx.position);
            if (quantity <= 0.0) reason = "no position available to sell";
            else if (ctx.position - quantity < -ctx.config.maxPosition) reason = "maximum short position exceeded";
        }
        if (!reason.empty()) {
            ctx.cycle.detail = reason;
            publishPublic(RiskRejected{token.generation, reason});
            publishToken(token, "rejected");
            return;
        }
        ctx.cycle.approvedQuantity = quantity;
        publishToken(token, "approved");
    });
    return true;
}

bool OrderNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        if (!accept(token)) return;
        auto& ctx = context();
        if (ctx.cycle.approvedQuantity <= 0.0) {
            fail(token.generation, QuantErrorCode::Internal, "risk node approved zero quantity");
            return;
        }
        Order order;
        order.orderId = ctx.strategyId + "-" + std::to_string(token.generation);
        order.symbol = ctx.config.symbol;
        order.side = ctx.cycle.signal == Signal::Buy ? Side::Buy : Side::Sell;
        order.quantity = ctx.cycle.approvedQuantity;
        order.referencePrice = ctx.lastPrice;
        order.generation = token.generation;
        ctx.cycle.order = order;
        publishPublic(OrderCreated{token.generation, order});
        publishToken(token, "created");
    });
    return true;
}

bool ExecutionNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        if (!accept(token)) return;
        auto& ctx = context();
        if (!ctx.cycle.order || !ctx.dependencies.executionVenue) {
            fail(token.generation, QuantErrorCode::DependencyFailure, "execution dependency or order missing");
            return;
        }
        try {
            auto result = ctx.dependencies.executionVenue->execute(*ctx.cycle.order,
                                                                   ctx.config.slippageBps);
            if (result.status == ExecutionResult::Status::Error) {
                fail(token.generation, QuantErrorCode::DependencyFailure, result.message);
            } else if (result.status == ExecutionResult::Status::Rejected) {
                ctx.cycle.detail = result.message.empty() ? "execution rejected" : result.message;
                publishToken(token, "rejected");
            } else {
                ctx.cycle.fill = result.fill;
                ctx.cycle.traded = true;
                publishPublic(FillReceived{token.generation, result.fill});
                publishToken(token, "filled");
            }
        } catch (const std::exception& e) {
            fail(token.generation, QuantErrorCode::DependencyFailure, e.what());
        }
    });
    return true;
}

bool PortfolioNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        if (!accept(token)) return;
        auto& ctx = context();
        if (!ctx.cycle.fill) {
            fail(token.generation, QuantErrorCode::Internal, "fill missing");
            return;
        }
        const auto& fill = *ctx.cycle.fill;
        const double notional = fill.quantity * fill.price;
        if (fill.side == Side::Buy) {
            ctx.cash -= notional;
            ctx.position += fill.quantity;
        } else {
            ctx.cash += notional;
            ctx.position -= fill.quantity;
        }
        publishPublic(PortfolioUpdated{token.generation, ctx.portfolioSnapshot()});
        publishToken(token, "updated");
    });
    return true;
}

bool PersistenceNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        if (!accept(token)) return;
        auto& ctx = context();
        if (!ctx.dependencies.store) {
            fail(token.generation, QuantErrorCode::DependencyFailure, "trading store missing");
            return;
        }
        std::string error;
        try {
            auto snapshot = ctx.portfolioSnapshot();
            if (!ctx.cycle.limitOnly) snapshot.completedTicks = ctx.completedTicks + 1;
            if (!ctx.dependencies.store->persist(ctx.strategyId, snapshot, error)) {
                fail(token.generation, QuantErrorCode::DependencyFailure,
                     error.empty() ? "snapshot persistence failed" : error);
                return;
            }
        } catch (const std::exception& e) {
            fail(token.generation, QuantErrorCode::DependencyFailure, e.what());
            return;
        }
        publishToken(token, "persisted");
    });
    return true;
}

bool CycleCompleteNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        auto& ctx = context();
        if (token.generation != ctx.generation || ctx.isTerminal()) return;
        if (!ctx.cycle.limitOnly) ++ctx.completedTicks;
        const bool exhausted = ctx.cycle.limitOnly || ctx.completedTicks >= ctx.config.maxTicks;
        ctx.busy = false;
        if (exhausted) ctx.state = StrategyState::Exhausted;
        auto snapshot = ctx.portfolioSnapshot();
        publishPublic(TickCompleted{token.generation, ctx.cycle.signal,
                                    ctx.cycle.traded, exhausted,
                                    ctx.cycle.detail, std::move(snapshot)});
        if (exhausted) publishPublic(StrategyStateChanged{ctx.state});
    });
    return true;
}

bool FailureNode::subscribeInputs() {
    subscribe<QuantFlowToken>([this](QuantFlowToken& token) {
        auto& ctx = context();
        if (token.generation != ctx.generation || ctx.isTerminal()) return;
        ctx.busy = false;
        ctx.state = StrategyState::Failed;
        const auto code = token.payload ? token.payload->errorCode : QuantErrorCode::Internal;
        const auto message = token.payload ? token.payload->message : "unknown workflow failure";
        publishPublic(StrategyFailed{token.generation, code, message});
        publishPublic(StrategyStateChanged{ctx.state});
    });
    return true;
}

}  // namespace quant_trading
