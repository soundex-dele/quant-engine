#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "quant_trading/api/quant_trading_api.h"

namespace {

using namespace quant_trading::api;

const char* signalName(ApiSignal signal) {
    switch (signal) {
        case ApiSignal::Buy: return "BUY";
        case ApiSignal::Sell: return "SELL";
        case ApiSignal::Hold: return "HOLD";
    }
    return "UNKNOWN";
}

const char* stateName(ApiStrategyState state) {
    switch (state) {
        case ApiStrategyState::Active: return "active";
        case ApiStrategyState::Paused: return "paused";
        case ApiStrategyState::Cancelled: return "cancelled";
        case ApiStrategyState::Failed: return "failed";
        case ApiStrategyState::Exhausted: return "exhausted";
        case ApiStrategyState::Stopped: return "stopped";
    }
    return "unknown";
}

const char* tickStatusName(ApiTickStatus status) {
    switch (status) {
        case ApiTickStatus::Accepted: return "accepted";
        case ApiTickStatus::Busy: return "busy";
        case ApiTickStatus::Paused: return "paused";
        case ApiTickStatus::Cancelled: return "cancelled";
        case ApiTickStatus::Exhausted: return "exhausted";
        case ApiTickStatus::NotFound: return "not-found";
        case ApiTickStatus::Stopped: return "stopped";
    }
    return "unknown";
}

const char* controlStatusName(ApiControlStatus status) {
    switch (status) {
        case ApiControlStatus::Accepted: return "accepted";
        case ApiControlStatus::AlreadyApplied: return "already-applied";
        case ApiControlStatus::InvalidState: return "invalid-state";
        case ApiControlStatus::NotFound: return "not-found";
        case ApiControlStatus::Stopped: return "stopped";
    }
    return "unknown";
}

void printEvent(const ApiEvent& event) {
    std::cout << std::fixed << std::setprecision(2);
    switch (event.type) {
        case ApiEventType::Created:
            std::cout << "[created] id=" << event.strategyId << '\n';
            break;
        case ApiEventType::StateChanged:
            std::cout << "[state] " << stateName(event.state) << '\n';
            break;
        case ApiEventType::TickAccepted:
            std::cout << "[tick] generation=" << event.generation
                      << " price=" << event.price
                      << " quantity=" << event.quantity << '\n';
            break;
        case ApiEventType::Signal:
            std::cout << "[signal] " << signalName(event.signal)
                      << " short_sma=" << event.shortAverage
                      << " long_sma=" << event.longAverage << '\n';
            break;
        case ApiEventType::RiskRejected:
            std::cout << "[risk-rejected] " << event.message << '\n';
            break;
        case ApiEventType::OrderCreated:
            std::cout << "[order] id=" << event.orderId
                      << " side=" << (event.side == ApiSide::Buy ? "BUY" : "SELL")
                      << " quantity=" << event.quantity
                      << " reference_price=" << event.price << '\n';
            break;
        case ApiEventType::Fill:
            std::cout << "[fill] order=" << event.orderId
                      << " quantity=" << event.quantity
                      << " price=" << event.price << '\n';
            break;
        case ApiEventType::Portfolio:
            std::cout << "[portfolio] cash=" << event.portfolio.cash
                      << " position=" << event.portfolio.position
                      << " equity=" << event.portfolio.equity << '\n';
            break;
        case ApiEventType::TickCompleted:
            std::cout << "[completed] generation=" << event.generation
                      << " signal=" << signalName(event.signal)
                      << " traded=" << (event.traded ? "yes" : "no")
                      << " ticks=" << event.portfolio.completedTicks;
            if (!event.message.empty()) std::cout << " detail=" << event.message;
            std::cout << '\n';
            break;
        case ApiEventType::Failed:
            std::cout << "[failed] generation=" << event.generation
                      << " message=" << event.message << '\n';
            break;
    }
}

struct LoadedConfig {
    std::string json;
    std::string symbol;
};

LoadedConfig loadConfig(const std::filesystem::path& inputPath) {
    const auto configPath = std::filesystem::absolute(inputPath).lexically_normal();
    std::ifstream input(configPath);
    if (!input) throw std::runtime_error("cannot open config: " + configPath.string());

    nlohmann::json config;
    input >> config;
    auto workflow = std::filesystem::path(config.at("workflow_path").get<std::string>());
    if (workflow.is_relative()) {
        auto fromWorkingDirectory = std::filesystem::absolute(workflow).lexically_normal();
        workflow = std::filesystem::exists(fromWorkingDirectory)
            ? fromWorkingDirectory
            : (configPath.parent_path() / workflow).lexically_normal();
    }
    config["workflow_path"] = workflow.string();
    return {config.dump(), config.at("symbol").get<std::string>()};
}

bool submit(QuantTradingApi& api, const std::string& strategyId,
            const std::string& symbol, std::int64_t timestamp,
            double price, double quantity) {
    const auto result = api.submitTick(
        strategyId, ApiMarketTick{symbol, timestamp, price, quantity});
    std::cout << "[submit] timestamp=" << timestamp
              << " status=" << tickStatusName(result.status) << '\n';
    return result.status == ApiTickStatus::Accepted;
}

bool runDemo(QuantTradingApi& api, const std::string& strategyId,
             const std::string& symbol) {
    std::cout << "Running SMA crossover demo...\n";
    const std::vector<double> prices{5.0, 4.0, 3.0, 2.0, 1.0, 4.0, 8.0};
    for (std::size_t i = 0; i < prices.size(); ++i) {
        if (!submit(api, strategyId, symbol, static_cast<std::int64_t>(i + 1),
                    prices[i], 100.0)) {
            return false;
        }
    }
    return true;
}

void printHelp() {
    std::cout << "Commands:\n"
              << "  tick <timestamp_ms> <price> [quantity]\n"
              << "  pause | resume | cancel\n"
              << "  help | quit\n";
}

void runInteractive(QuantTradingApi& api, const std::string& strategyId,
                    const std::string& symbol) {
    printHelp();
    std::string line;
    while (std::cout << "quant> " << std::flush, std::getline(std::cin, line)) {
        std::istringstream command(line);
        std::string name;
        command >> name;
        if (name.empty()) continue;
        if (name == "quit" || name == "exit") break;
        if (name == "help") { printHelp(); continue; }
        if (name == "tick") {
            std::int64_t timestamp = 0;
            double price = 0.0;
            double quantity = 1.0;
            if (!(command >> timestamp >> price)) {
                std::cout << "usage: tick <timestamp_ms> <price> [quantity]\n";
                continue;
            }
            command >> quantity;
            submit(api, strategyId, symbol, timestamp, price, quantity);
            continue;
        }
        ApiControlResult result;
        if (name == "pause") result = api.pause(strategyId);
        else if (name == "resume") result = api.resume(strategyId);
        else if (name == "cancel") result = api.cancel(strategyId);
        else {
            std::cout << "unknown command: " << name << '\n';
            continue;
        }
        std::cout << "[control] " << name << ' '
                  << controlStatusName(result.status) << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path configPath =
            std::filesystem::path(GRAPHFLOW_SOURCE_DIR) /
            "examples/quant_trading_config.json";
        bool interactive = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--interactive" || argument == "-i") interactive = true;
            else if (argument == "--help" || argument == "-h") {
                std::cout << "usage: quant_trading_cli [config.json] [--interactive]\n";
                return 0;
            } else {
                configPath = argument;
            }
        }

        const auto config = loadConfig(configPath);
        QuantTradingApi api;
        auto subscription = api.subscribe(printEvent);
        const auto created = api.createStrategy(config.json);
        if (created.status != ApiCreateStatus::Accepted) {
            std::cerr << "create strategy failed: " << created.errorMessage << '\n';
            return 1;
        }

        const bool ok = interactive
            ? (runInteractive(api, created.strategyId, config.symbol), true)
            : runDemo(api, created.strategyId, config.symbol);
        api.stop();
        if (subscription.unsubscribe) subscription.unsubscribe();
        return ok ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
