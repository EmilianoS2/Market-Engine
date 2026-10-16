#include "market_engine/quote_policy.hpp"
#include <cassert>
#include <iostream>
using namespace market_engine;
template<class F> void rejects(F action) { bool threw = false; try { action(); } catch (const std::exception &) { threw = true; } assert(threw); }
int main() {
    MarketMakerConfig config; config.validate();
    auto base = calculateQuotes(config, 10000, 100); assert(base.bid == 9750 && base.ask == 10250);
    auto excess = calculateQuotes(config, 10000, 150); assert(excess.bid == 9650 && excess.ask == 10150);
    auto scarce = calculateQuotes(config, 10000, 50); assert(scarce.bid == 9850 && scarce.ask == 10350);
    auto passive = calculateQuotes(config, 10000, 100, {10400, 9700}); assert(passive.bid == 9699 && passive.ask == 10401);
    assert(!calculateQuotes(config, 10000, 100, {{}, 1}).bid);
    auto fractional = calculateQuotes(config, 10001, 100); assert(fractional.bid == 9750 && fractional.ask == 10252);
    auto invalid = config; invalid.spreadBps = 0; rejects([&] { invalid.validate(); });
    invalid = config; invalid.maximumInventory = 99; rejects([&] { invalid.validate(); });
    invalid = config; invalid.referencePrice = UINT64_MAX; rejects([&] { invalid.validate(); });
    invalid = config; invalid.skewBpsPerShare = UINT64_MAX; rejects([&] { invalid.validate(); });
    invalid = config; invalid.refreshIntervalMs = 0; rejects([&] { invalid.validate(); });
    std::cout << "PASS quote policy: rounding, skew, passive prices, invalid config, overflow\n";
}
