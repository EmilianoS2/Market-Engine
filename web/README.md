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

## Simulated traffic

**Recent trades** starts collapsed. Click its heading to expand or collapse the latest 30 trades; the expanded table scrolls so it stays compact as fills arrive. Candles stay close together for a short history and fit up to 48 candles across the chart. Hover over a candle to inspect its exact time and OHLCV values.

Turn on **Simulated traffic** near the top of the page and choose Slow, Normal, or Busy. The simulator seeds missing bids and asks, then mixes limit orders that rest in the book with orders that cross the spread and execute. It also cancels older simulated orders to keep the book from growing indefinitely. You can place your own orders while it runs; they can match the simulated orders.

Fills come from the C++ matching engine and are saved to `trades.csv`, just like manually entered orders. Every fill updates the current candle's high, low, close, and volume. Candles use real one-minute intervals: wait until the next minute for the first fill to start the next candle.

Traffic starts off. Switching it off stops new actions after any submitted action finishes, leaving the current book and candles visible. It pauses when the tab is hidden, resumes when you return, and stops when you close or reload the page. **Clear book** also turns traffic off and clears live data; saved CSV trades remain. Simulator order IDs use the upper half of the 64-bit ID range, leaving small IDs convenient for manual orders.

The original test program still runs on its own:

```sh
clang++ -std=c++20 FINALorderbook.cpp -o FINALorderbook
./FINALorderbook
```
