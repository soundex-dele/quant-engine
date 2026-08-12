#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "core/graph_node.h"
#include "quant_trading/runtime/quant_runtime_context.h"

namespace quant_trading {

class QuantNode : public graphflow::core::GraphNode {
public:
    QuantNode(std::string name, graphflow::core::EventBus* bus,
              graphflow::core::Blackboard* blackboard,
              std::shared_ptr<QuantRuntimeContext> context,
              nlohmann::json params = {});

protected:
    bool doInit() override;
    bool doStart() override { return true; }
    void doStop() override {}
    void doRelease() override { subscribed_ = false; }
    virtual bool subscribeInputs() = 0;

    bool accept(const QuantFlowToken& token) const noexcept;
    void publishToken(const QuantFlowToken& token, std::string_view condition);
    void fail(std::uint64_t generation, QuantErrorCode code, std::string message);
    void publishPublic(QuantEvent event);
    QuantRuntimeContext& context() noexcept { return *context_; }
    const nlohmann::json& params() const noexcept { return params_; }

private:
    std::shared_ptr<QuantRuntimeContext> context_;
    nlohmann::json params_;
    bool subscribed_ = false;
};

class StrategyControlNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class MarketIngressNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class IndicatorNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class SignalNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class RiskNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class OrderNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class ExecutionNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class PortfolioNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class PersistenceNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class CycleCompleteNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };
class FailureNode final : public QuantNode { using QuantNode::QuantNode; bool subscribeInputs() override; };

}  // namespace quant_trading
