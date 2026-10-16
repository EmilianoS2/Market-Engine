# Saved spread and inventory-policy experiment

Run `python3 run_market_maker.py demo --output build/a-fresh-run` from the project root. Every configuration replays the same `market_maker_scenario.csv`: 62 ordered events, balanced flow, one-sided selling, and reference shocks from 100.00 to 110.00 to 90.00. Initial cash is 10,000.00 and initial inventory is 100 shares; holdings are constrained to 0–150. All trader remainders have explicit cancellations.

The results below use identical initial conditions, refresh interval, quote size, and trader willingness limits. Cash, P&L, excess return, and drawdown are in virtual currency (100 integer ticks = 1.00).

| Skew bps/share | Full spread bps | Buy/sell fills | Bought/sold shares | Ending inventory | Ending cash | Marked P&L | Excess vs holding | Max drawdown |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 100 | 18 / 9 | 80 / 35 | 145 | 5,507.25 | -1,442.75 | -442.75 | 3,000.00 |
| 0 | 300 | 18 / 8 | 78 / 33 | 145 | 5,615.75 | -1,334.25 | -334.25 | 3,000.00 |
| 0 | 500 | 16 / 7 | 75 / 30 | 145 | 5,711.25 | -1,238.75 | -238.75 | 3,000.00 |
| 2 | 100 | 18 / 9 | 82 / 37 | 145 | 5,524.53 | -1,425.47 | -425.47 | 3,002.25 |
| 2 | 300 | 17 / 8 | 79 / 34 | 145 | 5,634.21 | -1,315.79 | -315.79 | 3,000.00 |
| 2 | 500 | 14 / 7 | 66 / 31 | 135 | 6,699.24 | -1,150.76 | -150.76 | 2,900.00 |

On this path, widening the spread changes which traders accept the quotes. Inventory skew also changes prices and which sides remain attractive; at 500 bps, it leaves the maker with ten fewer shares before the final mark. The downward reference shock reduces marked portfolio value despite collecting spreads. Every run has zero bot quote rejections and respects finite resources/inventory limits. Fills count execution events, not completed round trips.

The final 5%/skew-2 run spends 10,090 simulated milliseconds with buys disabled by its resource/inventory policy. The comparable skew-0 run spends 30,090 milliseconds with buys disabled. Per-event quotes, cash, reservations, references, and disabled times are in the generated metrics CSVs.

One local optimized build took approximately 426–476 microseconds per complete scenario, including its CSV trade writes; this excludes compilation. Wall timings vary by machine and are printed separately from deterministic data. The test suite replays all six configurations twice and checks every generated CSV byte for equality.

This is one fixed fictional scenario with unlimited-resource counterparties and a finite maker account. It measures the implemented policy on that scenario; it does not estimate expected profitability or enforce a complete exchange-wide counterparty ledger.
