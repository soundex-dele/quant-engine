#include "quant_trading/quant_engine.h"

#include <atomic>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "quant_trading/quant_workflow.h"

namespace quant_trading {

struct QuantTradingEngine::Impl {
    struct Entry {
        std::shared_ptr<QuantTradingWorkflow> workflow;
        QuantSubscription subscription;
    };
    struct Subscribers {
        std::mutex mutex;
        std::uint64_t nextId = 1;
        std::unordered_map<std::uint64_t, QuantEngineEventHandler> handlers;
    };

    explicit Impl(QuantDependencies value)
        : dependencies(std::move(value)), subscribers(std::make_shared<Subscribers>()) {}

    void dispatch(const StrategyId& id, const QuantEvent& event) {
        std::vector<QuantEngineEventHandler> copy;
        {
            std::lock_guard<std::mutex> lock(subscribers->mutex);
            for (const auto& item : subscribers->handlers) copy.push_back(item.second);
        }
        QuantEngineEvent wrapped{id, event};
        for (auto& handler : copy) {
            try { handler(wrapped); } catch (...) {}
        }
    }

    std::shared_ptr<QuantTradingWorkflow> find(const StrategyId& id) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = entries.find(id);
        return it == entries.end() ? nullptr : it->second.workflow;
    }

    QuantDependencies dependencies;
    std::mutex mutex;
    std::unordered_map<StrategyId, Entry> entries;
    std::shared_ptr<Subscribers> subscribers;
    std::atomic<std::uint64_t> nextId{1};
    bool stopped = false;
};

QuantTradingEngine::QuantTradingEngine(QuantDependencies dependencies)
    : impl_(std::make_unique<Impl>(std::move(dependencies))) {}

QuantTradingEngine::~QuantTradingEngine() { stop(); }

StrategyId QuantTradingEngine::createStrategy(QuantConfig config) {
    config.validate();
    std::ostringstream stream;
    stream << "strategy-" << std::setw(6) << std::setfill('0') << impl_->nextId++;
    const auto id = stream.str();
    auto workflow = std::make_shared<QuantTradingWorkflow>(id, std::move(config),
                                                           impl_->dependencies);
    auto subscription = workflow->subscribe(
        [this, id](const QuantEvent& event) { impl_->dispatch(id, event); });
    if (!workflow->initialize() || !workflow->start()) {
        throw std::runtime_error("failed to initialize quant trading workflow");
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->stopped) {
            workflow->stop();
            throw std::runtime_error("quant trading engine is stopped");
        }
        impl_->entries.emplace(id, Impl::Entry{workflow, std::move(subscription)});
    }
    impl_->dispatch(id, StrategyCreated{});
    return id;
}

DestroyStatus QuantTradingEngine::destroyStrategy(const StrategyId& id) {
    Impl::Entry entry;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->stopped) return DestroyStatus::Stopped;
        auto it = impl_->entries.find(id);
        if (it == impl_->entries.end()) return DestroyStatus::NotFound;
        entry = std::move(it->second);
        impl_->entries.erase(it);
    }
    entry.workflow->stop();
    if (entry.subscription.unsubscribe) entry.subscription.unsubscribe();
    return DestroyStatus::Accepted;
}

TickSubmitResult QuantTradingEngine::submitTick(const StrategyId& id, MarketTick tick) {
    auto workflow = impl_->find(id);
    if (workflow) return workflow->submitTick(std::move(tick));
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return {impl_->stopped ? TickSubmitStatus::Stopped : TickSubmitStatus::NotFound, 0};
}

ControlResult QuantTradingEngine::pause(const StrategyId& id) {
    auto workflow = impl_->find(id);
    if (workflow) return workflow->pause();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return {impl_->stopped ? ControlStatus::Stopped : ControlStatus::NotFound};
}
ControlResult QuantTradingEngine::resume(const StrategyId& id) {
    auto workflow = impl_->find(id);
    if (workflow) return workflow->resume();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return {impl_->stopped ? ControlStatus::Stopped : ControlStatus::NotFound};
}
ControlResult QuantTradingEngine::cancel(const StrategyId& id) {
    auto workflow = impl_->find(id);
    if (workflow) return workflow->cancel();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return {impl_->stopped ? ControlStatus::Stopped : ControlStatus::NotFound};
}

QuantSubscription QuantTradingEngine::subscribe(QuantEngineEventHandler handler) {
    const auto state = impl_->subscribers;
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        id = state->nextId++;
        state->handlers.emplace(id, std::move(handler));
    }
    return QuantSubscription{[weak = std::weak_ptr<Impl::Subscribers>(state), id] {
        if (auto locked = weak.lock()) {
            std::lock_guard<std::mutex> guard(locked->mutex);
            locked->handlers.erase(id);
        }
    }};
}

void QuantTradingEngine::stop() noexcept {
    std::vector<Impl::Entry> entries;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->stopped) return;
        impl_->stopped = true;
        for (auto& item : impl_->entries) entries.push_back(std::move(item.second));
        impl_->entries.clear();
    }
    for (auto& entry : entries) {
        entry.workflow->stop();
        if (entry.subscription.unsubscribe) entry.subscription.unsubscribe();
    }
}

}  // namespace quant_trading
