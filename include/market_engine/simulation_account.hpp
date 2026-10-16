#pragma once
#include "market_maker_config.hpp"
#include "simulation_types.hpp"
#include <unordered_set>

namespace market_engine {
class SimulationAccount {
public:
    SimulationAccount(std::string owner, std::uint64_t cash, Quantity inventory);
    const std::string &owner() const { return owner_; }
    std::uint64_t cash() const { return cash_; }
    Quantity inventory() const { return inventory_; }
    // Only an explicit simulation reset seeds fresh endowments.
    void reset(std::uint64_t cash, Quantity inventory);
    bool apply(const Execution &execution);
    Commitments commitments(const std::vector<OpenOrder> &orders) const;
    Quantity permittedBuy(Price price, Quantity size, const MarketMakerConfig &config,
                          const std::vector<OpenOrder> &orders) const;
    Quantity permittedSell(Quantity size, const MarketMakerConfig &config,
                           const std::vector<OpenOrder> &orders) const;
    std::uint64_t portfolioValue(Price reference) const;
private:
    std::string owner_;
    std::uint64_t cash_;
    Quantity inventory_;
    std::unordered_set<std::uint64_t> applied_;
};
}
