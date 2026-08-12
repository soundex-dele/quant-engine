#include "quant_trading/quant_workflow.h"

#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/graph_engine.h"
#include "engine/node_config.h"
#include "quant_trading/nodes/node_creators.h"
#include "quant_trading/runtime/quant_runtime_context.h"

namespace quant_trading {
namespace {

bool validateContract(const nlohmann::json& graph) {
    static const std::unordered_map<std::string, std::string> required = {
        {"strategy_control", "QuantStrategyControlNode"},
        {"market_ingress", "QuantMarketIngressNode"},
        {"calculate_indicator", "QuantIndicatorNode"},
        {"generate_signal", "QuantSignalNode"},
        {"check_risk", "QuantRiskNode"},
        {"create_order", "QuantOrderNode"},
        {"execute_order", "QuantExecutionNode"},
        {"update_portfolio", "QuantPortfolioNode"},
        {"persist_snapshot", "QuantPersistenceNode"},
        {"cycle_complete", "QuantCycleCompleteNode"},
        {"failure_terminal", "QuantFailureNode"}
    };
    if (!graph.contains("nodes") || !graph.at("nodes").is_array()) return false;
    std::unordered_map<std::string, std::string> actual;
    for (const auto& node : graph.at("nodes")) {
        if (!node.contains("id") || !node.contains("class")) return false;
        actual[node.at("id").get<std::string>()] = node.at("class").get<std::string>();
    }
    for (const auto& item : required) {
        const auto it = actual.find(item.first);
        if (it == actual.end() || it->second != item.second) return false;
    }
    return true;
}

}  // namespace

struct QuantTradingWorkflow::Subscribers {
    std::mutex mutex;
    std::uint64_t nextId = 1;
    std::unordered_map<std::uint64_t, QuantEventHandler> handlers;
};

QuantTradingWorkflow::QuantTradingWorkflow(StrategyId id, QuantConfig config,
                                           QuantDependencies dependencies)
    : strategyId_(std::move(id)), config_(std::move(config)),
      dependencies_(std::move(dependencies)),
      subscribers_(std::make_shared<Subscribers>()) {}

QuantTradingWorkflow::~QuantTradingWorkflow() { stop(); }

bool QuantTradingWorkflow::initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_) return true;
    if (!dependencies_.executionVenue || !dependencies_.store) return false;

    std::ifstream input(config_.workflowPath);
    if (!input) return false;
    nlohmann::json graph;
    try { input >> graph; } catch (...) { return false; }
    if (!validateContract(graph)) return false;

    context_ = std::make_shared<QuantRuntimeContext>(strategyId_, config_, dependencies_);
    engine_ = std::make_unique<graphflow::engine::GraphEngine>();
    registerQuantNodeClasses(*engine_);
    engine_->setConfigBuilder(
        [this](const std::string& nodeClass, const nlohmann::json& nodeJson) {
            return graphflow::engine::NodeConfig::builder()
                .nodeClass(nodeClass)
                .nodeName(nodeJson.at("id").get<std::string>())
                .eventBus(engine_->eventBus())
                .blackboard(engine_->blackboard())
                .params(nodeJson.value("params", nlohmann::json::object()))
                .runtimeContext(context_)
                .build();
        });
    if (!engine_->loadFromJson(graph) || !engine_->init()) {
        engine_->release();
        engine_.reset();
        context_.reset();
        return false;
    }
    eventBus_ = engine_->eventBus();
    eventBus_->subscribeAll<WorkflowPublicEvent>(
        QuantTopics::PublicEvent, [this](WorkflowPublicEvent& value) {
            dispatch(value.event);
        });
    initialized_ = true;
    return true;
}

bool QuantTradingWorkflow::start() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_ || !engine_) return false;
    if (started_) return true;
    started_ = engine_->start();
    return started_;
}

void QuantTradingWorkflow::stop() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!engine_) return;
    if (context_ && !context_->isTerminal()) {
        context_->state = StrategyState::Stopped;
        dispatch(StrategyStateChanged{context_->state});
    }
    engine_->stop();
    eventBus_ = nullptr;
    engine_->release();
    engine_.reset();
    started_ = false;
    initialized_ = false;
}

TickSubmitResult QuantTradingWorkflow::submitTick(MarketTick tick) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!started_ || !eventBus_) return {TickSubmitStatus::Stopped, 0};
    auto response = std::make_shared<TickSubmitResult>();
    eventBus_->publish(std::string(QuantTopics::MarketTick),
                       MarketTickRequested{std::move(tick), response});
    return *response;
}

ControlResult QuantTradingWorkflow::control(ControlAction action) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!started_ || !eventBus_) return {ControlStatus::Stopped};
    auto response = std::make_shared<ControlResult>();
    eventBus_->publish(std::string(QuantTopics::Control),
                       StrategyControlRequested{action, response});
    return *response;
}

ControlResult QuantTradingWorkflow::pause() { return control(ControlAction::Pause); }
ControlResult QuantTradingWorkflow::resume() { return control(ControlAction::Resume); }
ControlResult QuantTradingWorkflow::cancel() { return control(ControlAction::Cancel); }

QuantSubscription QuantTradingWorkflow::subscribe(QuantEventHandler handler) {
    const auto state = subscribers_;
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        id = state->nextId++;
        state->handlers.emplace(id, std::move(handler));
    }
    return QuantSubscription{[weak = std::weak_ptr<Subscribers>(state), id] {
        if (auto locked = weak.lock()) {
            std::lock_guard<std::mutex> guard(locked->mutex);
            locked->handlers.erase(id);
        }
    }};
}

void QuantTradingWorkflow::dispatch(const QuantEvent& event) {
    std::vector<QuantEventHandler> handlers;
    {
        std::lock_guard<std::mutex> lock(subscribers_->mutex);
        for (const auto& item : subscribers_->handlers) handlers.push_back(item.second);
    }
    for (auto& handler : handlers) {
        try { handler(event); } catch (...) {}
    }
}

StrategyState QuantTradingWorkflow::state() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return context_ ? context_->state : StrategyState::Stopped;
}

PortfolioSnapshot QuantTradingWorkflow::portfolio() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return context_ ? context_->portfolioSnapshot() : PortfolioSnapshot{};
}

}  // namespace quant_trading
