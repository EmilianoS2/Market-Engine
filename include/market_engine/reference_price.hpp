#pragma once
#include "order_book.hpp"

namespace market_engine {
// Independent fictional valuation input. Executions never set this value.
class ReferencePrice {
public:
    explicit ReferencePrice(Price price) { set(price); }
    Price value() const { return price_; }
    void set(Price price) {
        if (!price) throw std::invalid_argument("Reference price must be positive.");
        price_ = price;
    }
private:
    Price price_ = 0;
};
}
