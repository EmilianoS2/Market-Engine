#include "market_engine/quote_policy.hpp"
#include "market_engine/checked_math.hpp"

namespace market_engine {
void MarketMakerConfig::validate() const {
    if (symbol.empty() || owner.empty()) throw std::invalid_argument("Symbol and owner are required.");
    if (!referencePrice || !quoteSize || !spreadBps || !refreshIntervalMs)
        throw std::invalid_argument("Reference, spread, size, and refresh interval must be positive.");
    if (minimumInventory > targetInventory || targetInventory > maximumInventory ||
        initialInventory < minimumInventory || initialInventory > maximumInventory)
        throw std::invalid_argument("Invalid inventory limits or initial inventory.");
    signedTicks(maximumInventory);
    signedTicks(checkedAdd(initialCash, checkedMultiply(initialInventory, referencePrice)));
    // Check both extreme skews before accepting a configuration.
    calculateQuotes(*this, referencePrice, minimumInventory);
    calculateQuotes(*this, referencePrice, maximumInventory);
}

QuotePrices calculateQuotes(const MarketMakerConfig &config, Price reference,
                           Quantity inventory, const BookSnapshot &external) {
    if (!reference || !config.spreadBps) throw std::invalid_argument("Reference and spread must be positive.");
    const auto product = checkedMultiply(reference, config.spreadBps);
    const auto halfWidth = std::max<std::uint64_t>(1, product / 20000 + (product % 20000 != 0));
    const auto deviation = inventory >= config.targetInventory ? inventory - config.targetInventory
                                                             : config.targetInventory - inventory;
    const auto shift = checkedMultiply(checkedMultiply(reference, config.skewBpsPerShare), deviation) / 10000;
    Price center;
    if (inventory >= config.targetInventory) {
        if (shift >= reference) throw std::invalid_argument("Inventory skew produces a nonpositive center.");
        center = reference - shift;
    } else center = checkedAdd(reference, shift);
    if (center <= halfWidth) throw std::invalid_argument("Spread produces a nonpositive bid.");
    QuotePrices quotes{center - halfWidth, checkedAdd(center, halfWidth)};
    if (external.bestAsk) {
        if (*external.bestAsk <= 1) quotes.bid.reset();
        else quotes.bid = std::min(*quotes.bid, *external.bestAsk - 1);
    }
    if (external.bestBid) {
        if (*external.bestBid == UINT64_MAX) quotes.ask.reset();
        else quotes.ask = std::max(*quotes.ask, *external.bestBid + 1);
    }
    if (quotes.bid && quotes.ask && *quotes.bid >= *quotes.ask)
        throw std::invalid_argument("Bot quotes must not cross.");
    return quotes;
}
}
