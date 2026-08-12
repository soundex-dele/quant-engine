#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/graph_engine.h"
#include "engine/node_config.h"
#include "quant_trading/nodes/node_creators.h"
#include "quant_trading/quant_workflow.h"

namespace quant_trading {
namespace {

std::string workflowPath() {
    return std::string(GRAPHFLOW_SOURCE_DIR) +
           "/examples/quant_trading_workflow.json";
}

QuantConfig config(std::uint64_t maxTicks = 10) {
    QuantConfig value;
    value.symbol = "TEST";
    value.workflowPath = workflowPath();
    value.shortWindow = 2;
    value.longWindow = 3;
    value.orderQuantity = 2.0;
    value.startingCash = 1000.0;
    value.maxPosition = 10.0;
    value.maxOrderNotional = 500.0;
    value.maxTicks = maxTicks;
    return value;
}

QuantDependencies dependencies() {
    return {makePaperExecutionVenue(), makeInMemoryTradingStore()};
}

class FailingStore final : public ITradingStore {
public:
    bool persist(const StrategyId&, const PortfolioSnapshot&,
                 std::string& error) override {
        error = "store offline";
        return false;
    }
};

class RejectingVenue final : public IExecutionVenue {
public:
    ExecutionResult execute(const Order&, double) override {
        return {ExecutionResult::Status::Rejected, {}, "venue rejected"};
    }
};

TEST(QuantConfigTest, RejectsInvalidWindows) {
    auto value = config();
    value.shortWindow = value.longWindow;
    EXPECT_THROW(value.validate(), std::invalid_argument);
}

TEST(QuantCreatorTest, MissingRuntimeContextFailsGraphLoad) {
    std::ifstream input(workflowPath());
    ASSERT_TRUE(input.good());
    nlohmann::json graph;
    input >> graph;
    graphflow::engine::GraphEngine engine;
    registerQuantNodeClasses(engine);
    engine.setConfigBuilder([&engine](const std::string& nodeClass,
                                      const nlohmann::json& nodeJson) {
        return graphflow::engine::NodeConfig::builder()
            .nodeClass(nodeClass)
            .nodeName(nodeJson.at("id").get<std::string>())
            .eventBus(engine.eventBus())
            .blackboard(engine.blackboard())
            .params(nodeJson.value("params", nlohmann::json::object()))
            .build();
    });
    EXPECT_FALSE(engine.loadFromJson(graph));
}

TEST(QuantWorkflowTest, RunsHoldRiskRejectAndFilledBranchesToTickLimit) {
    QuantTradingWorkflow workflow("strategy-test", config(4), dependencies());
    std::vector<QuantEvent> events;
    auto subscription = workflow.subscribe(
        [&events](const QuantEvent& event) { events.push_back(event); });
    ASSERT_TRUE(workflow.initialize());
    ASSERT_TRUE(workflow.start());

    EXPECT_EQ(workflow.submitTick({"TEST", 1, 3.0, 1.0}).status,
              TickSubmitStatus::Accepted);
    EXPECT_EQ(workflow.submitTick({"TEST", 2, 2.0, 1.0}).status,
              TickSubmitStatus::Accepted);
    EXPECT_EQ(workflow.submitTick({"TEST", 3, 1.0, 1.0}).status,
              TickSubmitStatus::Accepted);
    EXPECT_EQ(workflow.submitTick({"TEST", 4, 5.0, 1.0}).status,
              TickSubmitStatus::Accepted);

    EXPECT_EQ(workflow.state(), StrategyState::Exhausted);
    EXPECT_DOUBLE_EQ(workflow.portfolio().position, 2.0);
    EXPECT_EQ(workflow.portfolio().completedTicks, 4u);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const QuantEvent& event) {
        return std::holds_alternative<RiskRejected>(event);
    }));
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const QuantEvent& event) {
        return std::holds_alternative<FillReceived>(event);
    }));
    ASSERT_TRUE(subscription.unsubscribe);
    subscription.unsubscribe();
}

TEST(QuantWorkflowTest, InvalidTickRoutesToTerminalFailure) {
    QuantTradingWorkflow workflow("invalid-tick", config(), dependencies());
    std::vector<QuantEvent> events;
    auto subscription = workflow.subscribe(
        [&events](const QuantEvent& event) { events.push_back(event); });
    ASSERT_TRUE(workflow.initialize());
    ASSERT_TRUE(workflow.start());
    EXPECT_EQ(workflow.submitTick({"WRONG", 1, 1.0, 1.0}).status,
              TickSubmitStatus::Accepted);
    EXPECT_EQ(workflow.state(), StrategyState::Failed);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const QuantEvent& event) {
        const auto* failure = std::get_if<StrategyFailed>(&event);
        return failure && failure->code == QuantErrorCode::InvalidTick;
    }));
}

TEST(QuantWorkflowTest, PauseResumeAndCancelAreIdempotent) {
    QuantTradingWorkflow workflow("controls", config(), dependencies());
    ASSERT_TRUE(workflow.initialize());
    ASSERT_TRUE(workflow.start());
    EXPECT_EQ(workflow.pause().status, ControlStatus::Accepted);
    EXPECT_EQ(workflow.pause().status, ControlStatus::AlreadyApplied);
    EXPECT_EQ(workflow.submitTick({"TEST", 1, 1.0, 1.0}).status,
              TickSubmitStatus::Paused);
    EXPECT_EQ(workflow.resume().status, ControlStatus::Accepted);
    EXPECT_EQ(workflow.cancel().status, ControlStatus::Accepted);
    EXPECT_EQ(workflow.cancel().status, ControlStatus::AlreadyApplied);
    EXPECT_EQ(workflow.submitTick({"TEST", 1, 1.0, 1.0}).status,
              TickSubmitStatus::Cancelled);
}

TEST(QuantWorkflowTest, PersistenceFailureIsTerminal) {
    auto deps = dependencies();
    deps.store = std::make_shared<FailingStore>();
    QuantTradingWorkflow workflow("store-failure", config(), std::move(deps));
    ASSERT_TRUE(workflow.initialize());
    ASSERT_TRUE(workflow.start());
    EXPECT_EQ(workflow.submitTick({"TEST", 1, 1.0, 1.0}).status,
              TickSubmitStatus::Accepted);
    EXPECT_EQ(workflow.state(), StrategyState::Failed);
}

TEST(QuantWorkflowTest, ExecutionRejectionCompletesWithoutChangingPortfolio) {
    auto deps = dependencies();
    deps.executionVenue = std::make_shared<RejectingVenue>();
    QuantTradingWorkflow workflow("venue-reject", config(4), std::move(deps));
    ASSERT_TRUE(workflow.initialize());
    ASSERT_TRUE(workflow.start());
    workflow.submitTick({"TEST", 1, 3.0, 1.0});
    workflow.submitTick({"TEST", 2, 2.0, 1.0});
    workflow.submitTick({"TEST", 3, 1.0, 1.0});
    workflow.submitTick({"TEST", 4, 5.0, 1.0});
    EXPECT_DOUBLE_EQ(workflow.portfolio().position, 0.0);
    EXPECT_EQ(workflow.state(), StrategyState::Exhausted);
}

TEST(QuantWorkflowTest, InvalidGraphContractIsRejected) {
    const auto path = std::filesystem::temp_directory_path() /
                      "graphflow_invalid_quant_graph.json";
    {
        std::ofstream output(path);
        output << R"({"nodes":[{"id":"market_ingress","class":"Wrong"}],"paths":[{"name":"x","path":[{"id":"market_ingress"}]}]})";
    }
    auto value = config();
    value.workflowPath = path.string();
    QuantTradingWorkflow workflow("invalid-graph", value, dependencies());
    EXPECT_FALSE(workflow.initialize());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

}  // namespace
}  // namespace quant_trading
