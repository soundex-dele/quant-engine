# quant-engine

基于 [GraphFlow C++](https://github.com/soundex-dele/graphflow-cpp) 的确定性量化工作流引擎。仓库提供 C++ 量化引擎、稳定的公共 facade、命令行演示、示例配置和测试。

> **能力边界**
>
> 当前版本是用于开发、演示和测试的 **paper-trading MVP**，不是实盘交易网关。默认实现不连接交易所或券商，不包含账户凭据，也不处理真实订单簿、部分成交、手续费、交易日历、行情缺口、公司行动或交易所对账。

## 核心能力

- 使用 GraphFlow JSON 图描述一条完整的量化处理链：行情接入、SMA 指标、信号生成、风控、下单、模拟成交、组合更新和快照持久化。
- 支持创建和销毁多个策略实例，以及 `submitTick`、`pause`、`resume`、`cancel`、`subscribe` 和 `stop`。
- 默认策略为短/长周期简单移动平均线交叉；默认风控覆盖持仓、订单名义金额、可用现金和是否允许做空。
- 默认执行场所按 tick 价格和配置的滑点立即模拟成交，默认存储为进程内存。
- 底层 `QuantTradingEngine` 支持注入自定义 `IExecutionVenue` 与 `ITradingStore`。
- `submitTick` 是同步调用：一次完整图遍历结束后返回；事件回调在调用线程执行，应尽快返回。

## 仓库关系

本仓库通过 `vendor/graphflow-cpp` Git submodule 固定 GraphFlow C++ 依赖；`graphflow-cpp` 自身还包含嵌套 submodule，因此克隆和更新时必须使用递归模式。

```text
quant-engine
└── vendor/graphflow-cpp
    └── utoolkit
```

## 环境要求

- Git
- CMake 3.16 或更高版本
- 支持 C++17 的编译器（MSVC、Clang 或 GCC）
- 仅在构建单元测试时需要可被 CMake `CONFIG` 模式发现的 GoogleTest

## 快速开始

### 递归克隆

```powershell
git clone --recurse-submodules https://github.com/soundex-dele/quant-engine.git
cd quant-engine
```

如果已经完成普通克隆：

```powershell
git submodule update --init --recursive
```

### 构建

默认构建静态库和 CLI，不构建 GoogleTest 测试：

```powershell
cmake -S . -B build -DQUANT_ENGINE_BUILD_CLI=ON
cmake --build build --config Release --parallel
```

### 运行演示

Visual Studio 等多配置生成器：

```powershell
.\build\Release\quant_engine_cli.exe
```

单配置生成器通常运行 `./build/quant_engine_cli`。默认演示读取 `examples/quant_trading_config.json`，依次提交一组确定性的价格并打印信号、订单、成交和组合事件。

指定配置或进入交互模式：

```powershell
.\build\Release\quant_engine_cli.exe .\examples\quant_trading_config.json
.\build\Release\quant_engine_cli.exe .\examples\quant_trading_config.json --interactive
```

交互命令：

```text
tick <timestamp_ms> <price> [quantity]
pause
resume
cancel
help
quit
```

CLI 会先从当前工作目录解析相对 `workflow_path`，找不到时再相对于配置文件所在目录解析。

## 测试

测试默认关闭。启用测试时需要 GoogleTest 的 CMake package：

```powershell
cmake -S . -B build-tests -DQUANT_ENGINE_BUILD_CLI=OFF -DQUANT_ENGINE_BUILD_TESTS=ON
cmake --build build-tests --config Release --target quant_engine_tests quant_engine_public_api_smoke --parallel
ctest --test-dir build-tests -C Release --output-on-failure
```

如 CMake 找不到 GoogleTest，请通过包管理器工具链、`GTest_DIR` 或 `CMAKE_PREFIX_PATH` 提供其 `GTestConfig.cmake`。

## 公共 C++ API

面向应用集成时优先使用 `quant_trading::api::QuantTradingApi`：

```cpp
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "quant_trading/api/quant_trading_api.h"

using namespace quant_trading::api;

int main() {
    std::ifstream input("examples/quant_trading_config.json");
    const std::string config{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};

    QuantTradingApi api;
    auto subscription = api.subscribe([](const ApiEvent& event) {
        std::cout << "event=" << static_cast<int>(event.type)
                  << " strategy=" << event.strategyId << '\n';
    });

    const auto created = api.createStrategy(config);
    if (created.status != ApiCreateStatus::Accepted) {
        std::cerr << created.errorMessage << '\n';
        return 1;
    }

    const auto tick = api.submitTick(
        created.strategyId, ApiMarketTick{"DEMO", 1, 100.0, 10.0});
    if (tick.status != ApiTickStatus::Accepted) {
        return 2;
    }

    api.stop();
    if (subscription.unsubscribe) {
        subscription.unsubscribe();
    }
}
```

`createStrategy` 接收 JSON 字符串，其中 `workflow_path` 必须能从进程当前工作目录解析，或由调用方预先转换为绝对路径。公共 facade 默认组合 paper venue 与内存 store；需要替换依赖时使用 `QuantTradingEngine(QuantDependencies)`。

配置字段包括 `symbol`、`workflow_path`、`short_window`、`long_window`、`order_quantity`、`starting_cash`、`max_position`、`max_order_notional`、`slippage_bps`、`max_ticks` 和 `allow_short`。其中须满足 `0 < short_window < long_window`，下单数量和风险限额为正，初始现金非负，`slippage_bps` 位于 `[0, 10000)`，且 `max_ticks > 0`。提交的 tick 必须匹配 symbol，时间戳严格递增，price/quantity 为正的有限数值。

配置示例见 [`examples/quant_trading_config.json`](examples/quant_trading_config.json)，工作流定义见 [`examples/quant_trading_workflow.json`](examples/quant_trading_workflow.json)。

## 作为 submodule 使用

在上层项目中递归引入本仓库：

```powershell
git submodule add https://github.com/soundex-dele/quant-engine.git vendor/quant-engine
git submodule update --init --recursive
```

然后在上层 `CMakeLists.txt` 中：

```cmake
set(QUANT_ENGINE_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(QUANT_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/quant-engine)

target_link_libraries(my_app PRIVATE quant-engine::api)
```

使用内部强类型接口时可链接 `quant-engine::quant-engine`。当前仓库会自行加入其 `vendor/graphflow-cpp`，上层项目不要再单独加入另一份 GraphFlow CMake 目录，否则会产生同名 target。

## CMake 选项与目标

| 名称 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `QUANT_ENGINE_BUILD_CLI` | option | `ON` | 构建命令行演示 |
| `QUANT_ENGINE_BUILD_TESTS` | option | `OFF` | 构建 GoogleTest 与公共 API smoke test |
| `quant_engine` | target | — | 量化工作流静态库 |
| `quant-engine::quant-engine` | alias | — | `quant_engine` 的命名空间别名 |
| `quant_engine_api` | target | — | 稳定公共 facade 静态库 |
| `quant-engine::api` | alias | — | `quant_engine_api` 的命名空间别名 |
| `quant_engine_cli` | target | CLI 开启时 | 确定性演示和交互式 CLI |

## 目录结构

```text
api/        公共 facade 头文件与实现
cli/        命令行入口
docs/       设计、边界和工作流说明
examples/   示例配置、JSON 工作流和节点清单
include/    引擎、领域类型、依赖接口和节点声明
src/        引擎、工作流、运行时上下文和节点实现
tests/      引擎测试与公共 API smoke test
vendor/     graphflow-cpp submodule
```

## 当前限制

- 默认实现只做确定性模拟成交和内存持久化，进程退出后状态不会保留。
- 每个策略实例绑定一个 symbol；同一实例一次最多处理一个 tick。
- 策略固定为 SMA crossover 和固定下单数量，不是通用策略脚本运行时。
- 实盘接入仍需要幂等键、精度规则、审计、持久化、对账、风控熔断和安全凭据管理等生产能力。

更完整的系统边界、节点契约和业务流程见 [`docs/design.md`](docs/design.md)。

## 相关仓库

- [graphflow-cpp](https://github.com/soundex-dele/graphflow-cpp)：C++ Core 与 GraphEngine
- [graphflow-python](https://github.com/soundex-dele/graphflow-python)：GraphEngine Python 封装
- [agent-engine](https://github.com/soundex-dele/agent-engine)：Agent Engine 及多语言 SDK
- [behaviortree](https://github.com/soundex-dele/behaviortree)：GraphFlow 行为树扩展
