# Local order book website

The website uses the `OrderBook` class in `../FINALorderbook.cpp`. The Python server starts a small C++ adapter and serves the page on your computer.

## Run

From the project root:

```sh
python3 web/server.py
```

Open <http://127.0.0.1:8000> in a browser. Press `Ctrl+C` in the terminal to stop the server. On Windows, use `py web\server.py` if `python3` is unavailable.

You need Python 3 and a C++20 compiler (`clang++` or `g++`) on your PATH. The server compiles the adapter when it starts. If port 8000 is in use, pass `--port 8001` and open the printed address.

Use the form to place buy or sell limit orders. The page shows price levels, recent trades, and a depth chart of resting orders. The chart adds quantities across price levels: bids accumulate toward lower prices and asks toward higher prices. Hover over a chart point to see its price and quantity. You can cancel a resting order by ID. Prices and quantities are positive whole numbers. The book is kept in memory for the life of the server.

The original test program still runs on its own:

```sh
clang++ -std=c++20 FINALorderbook.cpp -o FINALorderbook
./FINALorderbook
```
