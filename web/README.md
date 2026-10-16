# Local order book website

The website uses the `OrderBook` class in `../FINALorderbook.cpp`. The Python server starts a small C++ adapter and serves the page on your computer.

## Run

From the project root:

```sh
python3 web/server.py
```

Open <http://127.0.0.1:8000> in a browser. Press `Ctrl+C` in the terminal to stop the server. On Windows, use `py web\server.py` if `python3` is unavailable.

You need Python 3 and a C++20 compiler (`clang++` or `g++`) on your PATH. The server compiles the adapter when it starts. If port 8000 is in use, pass `--port 8001` and open the printed address.

Use the form to place buy or sell limit orders. The page shows price levels, recent trades, a depth chart of resting orders, and one-minute candlesticks with volume. The depth chart adds quantities across price levels: bids accumulate toward lower prices and asks toward higher prices. Hover over a chart point or candle to inspect its values. You can cancel a resting order by ID. Prices and quantities are positive whole numbers. The book and candles are kept in memory for the life of the server. Filled trades are appended to `trades.csv` in the directory where you run the server, and remain there after resetting the live book.

## Market orders

Choose **Market** in the order form, select Buy or Sell, and enter an ID and quantity. The price field is hidden because market orders execute at the best available prices. They can fill across several levels; any unfilled quantity is cancelled instead of resting. Start the maker first to supply liquidity in an empty book. The result message reports filled and cancelled shares, and real fills update trades, the maker's account, the CSV, and candles. The maker itself continues to post limit orders. The Market option requires the updated backend; restart the server and refresh after code changes.

## Market-maker controls

On the one-minute candle chart, enable **Live maker limits** to show a green horizontal buy line and a red horizontal sell line for the maker's actual resting orders. Labels show price in ticks and shares remaining; hover over a label to see its order ID. The chart includes quote prices in its scale and can display quotes before the first trade without creating a candle. Lines update after fills/replacements and disappear when the corresponding quote is filled or cancelled. These are the current resting orders displayed across the chart, so use recent trades to inspect earlier executions. Turning the overlay off restores the candle-only view.

Click **Start maker** to post the finite-account bot's bids and asks. Its settings and virtual balances are on the page; the book's **MM qty** column and trade owners identify its activity. At the defaults, it quotes 9,750 / 10,250 ticks (97.50 / 102.50 virtual currency), five shares per side. Enable simulated traffic or place orders manually to produce executions. While the maker runs, simulated trader limits are sampled around the independent reference, so a wider spread is not automatically accepted.

Stopping cancels bot orders and preserves cash/shares; Clear book explicitly reseeds the account and stops both maker and traffic. Reference changes affect future quotes and marked valuation, while Last trade changes only after an execution. See [the project README](../README.md) for architecture, risk rules, tests, and the saved deterministic experiment.

## Simulated traffic controls

**Recent trades** starts collapsed. Click its heading to expand or collapse the latest 30 trades; the expanded table scrolls so it stays compact as fills arrive. Candles stay close together for a short history and fit up to 48 candles across the chart. Hover over a candle to inspect its exact time and OHLCV values.

Turn on **Simulated traffic** near the top of the page and choose Slow, Normal, or Busy. The simulator seeds missing bids and asks, then mixes limit orders that rest in the book with orders that cross the spread and execute. It also cancels older simulated orders to keep the book from growing indefinitely. You can place your own orders while it runs; they can match the simulated orders.

Fills come from the C++ matching engine and are saved to `trades.csv`, just like manually entered orders. Every fill updates the current candle's high, low, close, and volume. Candles use real one-minute intervals: wait until the next minute for the first fill to start the next candle.

Traffic starts off. Switching it off stops new actions after any submitted action finishes, leaving the current book and candles visible. It pauses when the tab is hidden, resumes when you return, and stops when you close or reload the page. **Clear book** also turns traffic off and clears live data; saved CSV trades remain. Simulator order IDs use the upper half of the 64-bit ID range, leaving small IDs convenient for manual orders.

The original test program still runs on its own:

```sh
clang++ -std=c++20 FINALorderbook.cpp -o FINALorderbook
./FINALorderbook
```
