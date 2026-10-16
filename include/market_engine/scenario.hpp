#pragma once
#include "market_maker.hpp"

namespace market_engine {
struct ScenarioEvent {
    std::uint64_t timeMs;
    std::string phase, action, owner;
    Order order{};
};
struct ScenarioResult {
    std::vector<Execution> executions;
    std::vector<Candle> candles;
    MakerMetrics metrics;
    std::uint64_t cash;
    Quantity inventory;
    std::string measurementsCsv;
};
std::vector<ScenarioEvent> readScenario(const std::string &filename);
ScenarioResult runScenario(const MarketMakerConfig &config, const std::vector<ScenarioEvent> &events, TradeCSVWriter *writer = nullptr);
}
