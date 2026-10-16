#pragma once
#include "order_book.hpp"

namespace market_engine {
struct MarketMakerConfig {
    std::string symbol = "LUFFY";
    std::string owner = "MM_LUFFY";
    Price referencePrice = 10000;
    std::uint64_t spreadBps = 500;
    Quantity quoteSize = 5;
    std::uint64_t initialCash = 1000000;
    Quantity initialInventory = 100;
    Quantity targetInventory = 100;
    Quantity minimumInventory = 0;
    Quantity maximumInventory = 150;
    std::uint64_t skewBpsPerShare = 2;
    std::uint64_t refreshIntervalMs = 1000;
    void validate() const;
};
}
