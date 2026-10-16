#pragma once
#include "simulation_account.hpp"
#include <unordered_set>

namespace market_engine {
class EngineAdapter {
public:
    EngineAdapter(OrderBook &book, SimulationAccount &account, const MarketMakerConfig &config);
    SubmitResult submit(const OrderRequest &request, Timestamp time);
    CancelResult cancel(OrderId id, const std::string &requester);
    std::vector<OpenOrder> openOrders(const std::string &owner = "") const;
    BookSnapshot snapshot(const std::string &excludeOwner = "") const;
    OrderId allocateBotId();
    void configure(const MarketMakerConfig &config) { config_ = config; }
    void reset();
private:
    OrderBook &book_;
    SimulationAccount &account_;
    MarketMakerConfig config_;
    std::map<OrderId, OpenOrder> orders_;
    std::unordered_set<OrderId> usedIds_;
    std::uint64_t nextExecution_ = 1;
    std::uint64_t nextArrival_ = 1;
    OrderId nextBotId_ = (1ULL << 62);
};
}
