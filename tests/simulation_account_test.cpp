#include "market_engine/simulation_account.hpp"
#include <cassert>
#include <iostream>
using namespace market_engine;
Execution execution(std::uint64_t id, bool buy, Price price, Quantity quantity) {
    return {id, id, {1, 2, price, quantity, Timestamp{}}, "LUFFY", 1, 2,
        buy ? "MM_LUFFY" : "TRADER", buy ? "TRADER" : "MM_LUFFY"};
}
int main() {
    MarketMakerConfig config;
    SimulationAccount account(config.owner, config.initialCash, config.initialInventory);
    std::vector<OpenOrder> open{{{1, Side::Buy, 9750, 5}, config.owner, config.symbol, 1, true},
                              {{2, Side::Sell, 10250, 5}, config.owner, config.symbol, 2, true}};
    auto reserved = account.commitments(open); assert(reserved.cash == 48750 && reserved.shares == 5);
    assert(account.cash() == 1000000 && account.inventory() == 100);
    auto sold = execution(1, false, 10250, 3); assert(account.apply(sold)); assert(!account.apply(sold));
    assert(account.cash() == 1030750 && account.inventory() == 97);
    open[1].order.remaining = 2; assert(account.commitments(open).shares == 2);
    open.erase(open.begin() + 1); assert(account.commitments(open).shares == 0);
    assert(account.cash() == 1030750 && account.inventory() == 97);
    SimulationAccount poor(config.owner, 20000, 100); assert(poor.permittedBuy(9750, 5, config, {}) == 2);
    SimulationAccount scarce(config.owner, 1000000, 2); assert(scarce.permittedSell(5, config, {}) == 2);
    SimulationAccount maximum(config.owner, 1000000, 150); assert(maximum.permittedBuy(9750, 5, config, {}) == 0);
    SimulationAccount minimum(config.owner, 1000000, 0); assert(minimum.permittedSell(5, config, {}) == 0);
    bool overflow = false;
    try { account.apply(execution(2, false, UINT64_MAX, 2)); } catch (const std::overflow_error &) { overflow = true; }
    assert(overflow && account.cash() == 1030750 && account.inventory() == 97);
    std::cout << "PASS account: exact fills, deduplication, commitments, caps, atomic overflow rejection\n";
}
