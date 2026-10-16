#pragma once
#include "engine_adapter.hpp"
#include "quote_policy.hpp"
#include "reference_price.hpp"

namespace market_engine {
enum class MakerState { Stopped, Running, Faulted };
struct MakerMetrics {
    std::uint64_t buyFills = 0, sellFills = 0;
    Quantity buyVolume = 0, sellVolume = 0;
    std::uint64_t rejections = 0;
    std::uint64_t buyDisabledMs = 0, sellDisabledMs = 0;
    Quantity minimumInventory = 0, maximumInventory = 0;
    std::uint64_t portfolioValue = 0, maximumDrawdown = 0;
    std::int64_t pnl = 0, excessReturn = 0;
};
class MarketMaker {
public:
    MarketMaker(MarketMakerConfig config, SimulationAccount &account, EngineAdapter &adapter, ReferencePrice &reference);
    void start(Timestamp time);
    void stop(Timestamp time);
    void refresh(Timestamp time);
    void advance(Timestamp time);
    void configure(const MarketMakerConfig &config, Timestamp time);
    void onExecutions(const std::vector<Execution> &executions, Timestamp time);
    void measure(Timestamp time);
    void reset(Timestamp time);
    MakerState state() const { return state_; }
    const std::string &fault() const { return fault_; }
    const MarketMakerConfig &config() const { return config_; }
    const MakerMetrics &metrics() const { return metrics_; }
    std::uint64_t nextDecisionMs() const { return nextDecisionMs_; }
    bool buyDisabled() const { return buyDisabled_; }
    bool sellDisabled() const { return sellDisabled_; }
private:
    void cancelQuotes();
    void fail(const std::string &reason, Timestamp time);
    MarketMakerConfig config_;
    SimulationAccount &account_;
    EngineAdapter &adapter_;
    ReferencePrice &reference_;
    MakerState state_ = MakerState::Stopped;
    std::string fault_;
    MakerMetrics metrics_;
    std::uint64_t initialValue_, initialCash_, peakValue_;
    Quantity initialInventory_;
    Price initialReference_;
    std::uint64_t nextDecisionMs_ = 0, lastMeasurementMs_ = 0;
    bool measured_ = false, buyDisabled_ = false, sellDisabled_ = false;
};
std::uint64_t milliseconds(Timestamp time);
Timestamp simulatedTime(std::uint64_t milliseconds);
}
