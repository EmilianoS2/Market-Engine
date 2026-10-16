# Market Engine

A C++20 order book with price/FIFO matching, partial fills, cancellation, CSV trade recording, one-minute OHLCV, a localhost website, simulated traders, and a finite-account LUFFY market maker.

## Run the website

```sh
python3 web/server.py --port 8001
```

Open <http://127.0.0.1:8001>. Python 3 and `clang++` or `g++` must be on your PATH. Windows can use `py` instead of `python3`. The existing Python server compiles and starts one C++ backend; all matching, strategy decisions, and account updates happen sequentially in that process.

Click **Start maker**, then turn on **Simulated traffic** or enter counterparty orders yourself. The book's **MM qty** column identifies the bot's liquidity. Recent trades show buyer/seller owners and can be expanded. Quotes alone never create trades, change Last trade, or create candles.

Prices use integer ticks, with one tick equal to 0.01 virtual currency. For example, a price of `10300` means 103.00. The maker panel displays cash and quote prices in virtual currency; order inputs and chart axes use ticks. Quantities are whole shares.

## Limit and market orders

The order form has a **Limit / Market** selector. A limit order requires a price and can rest in the book. A market order requires only a side and quantity, consumes the best opposing prices in FIFO order at each level, and never rests. Large market orders can fill at several prices. If liquidity runs out, any unfilled quantity is cancelled immediately. An empty book produces zero fills and cancels the whole quantity; it does not invent a price or trade.

The order result reports filled, cancelled, and resting quantities separately. Actual market fills use the existing ownership/accounting, CSV, and OHLCV pipeline. The maker still posts passive limit orders; it cannot submit market orders. A market order that would reach one of its owner's own resting orders is rejected before any fill. An own quote farther away than the requested quantity can reach does not block the market order.

In C++, existing four-field `Order` initializers remain limit orders. To submit a market buy:

```cpp
auto trades = book.addOrder({42, Side::Buy, 0, 8, OrderType::Market});
```

Zero is the absence of a limit price here; it is never a resting or execution price. The adapter exposes the filled/cancelled quantities in its submission result. The HTTP equivalent is `{"id":"42","side":"buy","type":"market","quantity":"8"}` at `/api/orders`, with no `price` field. Omitting `type` keeps the existing limit-order behavior. Restart the server after backend changes; the Market option stays disabled if the running backend does not support it.

## Default market-maker settings

| Setting | Default |
| --- | --- |
| Symbol / owner | `LUFFY` / `MM_LUFFY` |
| Independent reference price | 10,000 ticks = 100.00 |
| Full spread | 500 basis points = 5% |
| Quote size per side | 5 shares |
| Initial cash | 1,000,000 ticks = 10,000.00 |
| Initial inventory / target | 100 / 100 shares |
| Minimum / maximum inventory | 0 / 150 shares |
| Inventory skew | 2 basis points per excess share |
| Refresh interval | 1,000 milliseconds |

Reference, spread, size, interval, skew, and inventory limits can be edited on the page. Initial cash/inventory and identity are configured in `include/market_engine/market_maker_config.hpp`; editing those requires restarting the server. Endowments are seeded explicitly once. **Stop maker** cancels its quotes and preserves its cash/shares. Restarting the maker does not top up resources. **Clear book** explicitly resets the whole live simulation, including the maker's seeded account. Existing `trades.csv` rows remain. A fault cancels bot quotes and requires Clear book before restarting.

At the defaults, the maker bids 9,750 and asks 10,250. A counterparty buying three shares at a limit of 10,300 executes at the resting ask, 10,250. The maker then has 1,030,750 cash ticks (10,307.50), 97 shares, and an ask remainder of two. An eligible partial remainder stays in place; changed desired prices or tighter risk limits can require replacement at the next decision.

The reference is a fictional valuation input, independent of candles and last trade. The policy uses `ceil(reference * spread_bps / 20000)` for the half-width, at least one tick. Its center moves down with excess inventory and up below the target, using signed truncation toward zero. Prices are clamped away from external opposing quotes, and a serialized post-only check rejects any immediate execution. Cash, share reservations, and inventory capacity limit quantities. There is at most one live bot order per side. Unchanged eligible quotes retain their IDs and FIFO arrival order.

## Modules

| File | Responsibility |
| --- | --- |
| `FINALorderbook.cpp`, `include/market_engine/order_book.hpp` | Existing matching/CSV/candle implementations and shared declarations; optional injected execution timestamp |
| `engine_adapter.hpp`, `src/engine_adapter.cpp` | Order ownership, unique IDs, live-order snapshots, post-only/resource checks, ordered attributed executions |
| `simulation_account.hpp`, `src/simulation_account.cpp` | The sole owner of the maker's cash/shares; exact fill accounting and reservations derived from remaining orders |
| `quote_policy.hpp`, `src/quote_policy.cpp` | Pure spread/skew/price calculation and configuration validation |
| `reference_price.hpp` | Independent reference-price provider |
| `market_maker.hpp`, `src/market_maker.cpp` | Quote lifecycle, risk-aware replacement, accounting reconciliation, measurements, and shutdown |
| `web/orderbook_backend.cpp` | Coordinates requests, maker decisions, every execution, CSV recording, and candles |
| `scenario.hpp`, `src/scenario.cpp` | Injected-clock scripted experiment coordinator |

Orders pass through the adapter and then normal `OrderBook::addOrder`. Ownership is resolved before completed orders leave the adapter lookup. Execution IDs/sequences identify distinct fills, even when they share a timestamp. Bot notifications never submit orders recursively. External/manual/simulated orders have participant owners too; orders cannot cross another order belonging to the same owner. IDs cannot be reused within a live simulation, even after a fill/cancellation. Clear book begins a fresh simulation.

The web backend checks for a due maker decision before each command, applies all resulting execution/account updates, feeds those executions to the original CSV/candle consumers, then measures the result. Browser polling drives decisions while the maker is running; this adds no backend strategy thread. Hidden tabs pause polling, and another active client can still drive the shared engine. Closing/reloading the page requests Stop maker with a best-effort keepalive request. Graceful server shutdown explicitly cancels all bot orders before exiting. Ctrl+C stops the server; restarting it starts a fresh live simulation.

## Tests and deterministic experiment

See [the saved experiment results](data/EXPERIMENT.md) for the six spread/skew comparisons.

```sh
python3 run_market_maker.py test
python3 run_market_maker.py demo
```

The test command runs the original engine/candle regressions, quote-policy/account tests, market-maker integration acceptance cases, byte-for-byte scenario determinism, and the real HTTP API test with a temporary CSV. It uses the existing assertion style and Python's standard library. It covers attribution, duplicate notifications, multi-fill commands, partial-fill commitments, cash/share/inventory caps, FIFO retention, replacement, post-only rejection without book mutation, ownership isolation, invalid/overflowing settings, fault recovery, and shutdown.

The demo replays `data/market_maker_scenario.csv` with full spreads of 100, 300, and 500 basis points and inventory skews of 0 and 2. Every run starts with identical cash, shares, reference, clock, and counterparty commands. Trader limits are fixed in the input CSV and do not automatically accept bot quotes. Cancellation times explicitly expire remaining trader orders. The scenario includes balanced flow, one-sided selling, and upward/downward reference shocks.

For each event time, the scenario runs earlier due decisions, applies reference changes at that time, runs a due maker decision, then executes counterparties in CSV order. All executions are reconciled, recorded, aggregated, and measured before advancing. It uses injected timestamps without sleeps; repeated executions/candles/balances/metrics are identical. Several fills may legitimately share a timestamp.

The demo writes `build/experiment/skew_<k>_spread_<s>_{trades,executions,candles,metrics}.csv`. Metrics include actual live prices/sizes, reference, inventory, cash, reservations, fills/volume per side, rejections, disabled-side time, marked value/P&L, maximum drawdown, inventory extremes, and excess return versus holding the initial inventory. Wall-clock execution timings are printed separately and excluded from deterministic CSVs. Use a fresh output directory for each run, because the existing raw trade recorder appends:

```sh
python3 run_market_maker.py demo --output build/another-run
```

Marked P&L is `cash + inventory * reference - initial_value`. Excess return subtracts the return from simply holding the initial shares. Quoted spread is not realized profit, and reference marking is not a guaranteed liquidation value. Counterparties have unlimited simulated resources in this harness; only the maker has a complete finite account. This is a fictional learning simulation. Authentication, exchange-wide ledgers, fees, short selling, crash recovery, multiple bots, and concurrent matching are outside this version.

The original standalone exercise still works:

```sh
clang++ -std=c++20 FINALorderbook.cpp -o FINALorderbook
./FINALorderbook
```
