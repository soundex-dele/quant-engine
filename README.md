# Quant Trading CLI

Build from the repository root:

```powershell
cmake -S . -B build -DENABLE_QUANT_TRADING=ON -DQUANT_TRADING_BUILD_CLI=ON
cmake --build build --config Debug --target quant_trading_cli --parallel 4
```

Run the deterministic SMA-crossover paper-trading demo:

```powershell
.\build\engine\quant_trading\Debug\quant_trading_cli.exe
```

Use a custom configuration:

```powershell
.\build\engine\quant_trading\Debug\quant_trading_cli.exe .\my_quant_config.json
```

Start the interactive shell:

```powershell
.\build\engine\quant_trading\Debug\quant_trading_cli.exe --interactive
```

Interactive commands:

```text
tick <timestamp_ms> <price> [quantity]
pause
resume
cancel
help
quit
```

The CLI calls only `quant_trading::api::QuantTradingApi`. A relative
`workflow_path` is first resolved from the current working directory and then
from the configuration file's directory.
# quant-engine

基于 GraphFlow 的量化交易工作流引擎。`vendor/graphflow-cpp` 是固定版本的 Git submodule。

```powershell
git clone --recurse-submodules https://github.com/soundex-dele/quant-engine.git
cmake -S . -B build
cmake --build build --config Debug
```
