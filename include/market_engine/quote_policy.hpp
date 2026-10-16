#pragma once
#include "market_maker_config.hpp"
#include "simulation_types.hpp"

namespace market_engine {
struct QuotePrices {
    std::optional<Price> bid;
    std::optional<Price> ask;
};
QuotePrices calculateQuotes(const MarketMakerConfig &config, Price reference,
                           Quantity inventory, const BookSnapshot &external = {});
}
