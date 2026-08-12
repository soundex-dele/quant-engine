#include "quant_trading/nodes/node_creators.h"

#include <memory>
#include <stdexcept>

#include "engine/graph_engine.h"
#include "engine/node_config.h"
#include "quant_trading/nodes/quant_nodes.h"

namespace quant_trading {
namespace {

template <typename NodeT>
class QuantNodeCreator final : public graphflow::engine::NodeCreator {
public:
    std::shared_ptr<graphflow::core::GraphNode> createNode(
        const graphflow::engine::NodeConfig& config) override {
        if (!config.eventBus || !config.blackboard || config.nodeName.empty()) {
            throw std::invalid_argument("quant node requires graph services and nodeName");
        }
        auto context = std::dynamic_pointer_cast<QuantRuntimeContext>(config.runtimeContext);
        if (!context) throw std::invalid_argument("quant node requires QuantRuntimeContext");
        return std::make_shared<NodeT>(config.nodeName, config.eventBus,
                                       config.blackboard, std::move(context),
                                       config.params);
    }
};

}  // namespace

void registerQuantNodeClasses(graphflow::engine::GraphEngine& engine) {
    engine.registerCreator("QuantStrategyControlNode", std::make_shared<QuantNodeCreator<StrategyControlNode>>());
    engine.registerCreator("QuantMarketIngressNode", std::make_shared<QuantNodeCreator<MarketIngressNode>>());
    engine.registerCreator("QuantIndicatorNode", std::make_shared<QuantNodeCreator<IndicatorNode>>());
    engine.registerCreator("QuantSignalNode", std::make_shared<QuantNodeCreator<SignalNode>>());
    engine.registerCreator("QuantRiskNode", std::make_shared<QuantNodeCreator<RiskNode>>());
    engine.registerCreator("QuantOrderNode", std::make_shared<QuantNodeCreator<OrderNode>>());
    engine.registerCreator("QuantExecutionNode", std::make_shared<QuantNodeCreator<ExecutionNode>>());
    engine.registerCreator("QuantPortfolioNode", std::make_shared<QuantNodeCreator<PortfolioNode>>());
    engine.registerCreator("QuantPersistenceNode", std::make_shared<QuantNodeCreator<PersistenceNode>>());
    engine.registerCreator("QuantCycleCompleteNode", std::make_shared<QuantNodeCreator<CycleCompleteNode>>());
    engine.registerCreator("QuantFailureNode", std::make_shared<QuantNodeCreator<FailureNode>>());
}

}  // namespace quant_trading
