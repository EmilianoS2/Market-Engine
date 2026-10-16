#include "market_engine/scenario.hpp"
#include <filesystem>
#include <iostream>

using namespace market_engine;
int main(int argc, char **argv) {
    try {
        const std::string input = argc > 1 ? argv[1] : "data/market_maker_scenario.csv";
        const std::filesystem::path output = argc > 2 ? argv[2] : "build/experiment";
        const auto events = readScenario(input);
        std::filesystem::create_directories(output);
        std::cout << "Unlimited-resource scripted counterparties; one LUFFY maker with finite cash/shares.\n";
        std::cout << "skew_bps_per_share,spread_bps,buy_fills,sell_fills,buy_volume,sell_volume,cash_ticks,inventory,pnl_ticks,excess_return_ticks,max_drawdown_ticks,rejections,buy_disabled_ms,sell_disabled_ms\n";
        for (auto skew : {0ULL, 2ULL}) for (auto spread : {100ULL, 300ULL, 500ULL}) {
            auto config = MarketMakerConfig{}; config.spreadBps = spread; config.skewBpsPerShare = skew;
            const auto prefix = output / ("skew_" + std::to_string(skew) + "_spread_" + std::to_string(spread));
            TradeCSVWriter writer(prefix.string() + "_trades.csv");
            const auto started = std::chrono::steady_clock::now();
            auto result = runScenario(config, events, &writer);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
            std::ofstream(prefix.string() + "_metrics.csv") << result.measurementsCsv;
            std::ofstream executions(prefix.string() + "_executions.csv");
            executions << "trade_id,sequence,timestamp_ms,symbol,buy_order_id,sell_order_id,buyer,seller,price_ticks,quantity\n";
            for (const auto &e : result.executions) executions << e.id << ',' << e.sequence << ',' << milliseconds(e.trade.ExecutedAt) << ',' << e.symbol << ',' << e.buyOrderId << ',' << e.sellOrderId << ',' << e.buyer << ',' << e.seller << ',' << e.trade.price << ',' << e.trade.quantity << '\n';
            std::ofstream candles(prefix.string() + "_candles.csv");
            candles << "start_time_ms,open,high,low,close,volume\n";
            for (const auto &c : result.candles) candles << milliseconds(c.startTime) << ',' << c.open << ',' << c.high << ',' << c.low << ',' << c.close << ',' << c.volume << '\n';
            const auto &m = result.metrics;
            std::cout << skew << ',' << spread << ',' << m.buyFills << ',' << m.sellFills << ',' << m.buyVolume << ',' << m.sellVolume << ',' << result.cash << ',' << result.inventory << ',' << m.pnl << ',' << m.excessReturn << ',' << m.maximumDrawdown << ',' << m.rejections << ',' << m.buyDisabledMs << ',' << m.sellDisabledMs << '\n';
            std::cerr << "Skew " << skew << ", spread " << spread << ": " << events.size() << " scripted commands in " << elapsed << " microseconds (wall time; excluded from deterministic data).\n";
        }
        std::cout << "CSV measurements, executions, raw trades, and candles: " << output.string() << '\n';
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
