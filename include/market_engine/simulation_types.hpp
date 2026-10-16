#pragma once
#include "order_book.hpp"
#include <optional>

namespace market_engine {
struct OrderRequest {
    Order order;
    std::string owner;
    std::string symbol = "LUFFY";
    bool postOnly = false;
};
struct OpenOrder {
    Order order;
    std::string owner;
    std::string symbol;
    std::uint64_t arrivalSequence;
    bool postOnly;
};
struct Execution {
    std::uint64_t id;
    std::uint64_t sequence;
    Trade trade;
    std::string symbol;
    OrderId buyOrderId;
    OrderId sellOrderId;
    std::string buyer;
    std::string seller;
};
struct BookSnapshot {
    std::optional<Price> bestBid;
    std::optional<Price> bestAsk;
};
enum class SubmitStatus { Rejected, Resting, PartiallyFilled, Filled, PartiallyFilledCancelled, Cancelled };
struct SubmitResult {
    SubmitStatus status = SubmitStatus::Rejected;
    std::string error;
    std::vector<Execution> executions;
    Quantity remaining = 0;
    Quantity filledQuantity = 0;
    Quantity cancelledQuantity = 0;
    bool accepted() const { return status != SubmitStatus::Rejected; }
};
enum class CancelStatus { Cancelled, NotOpen, NotOwner };
struct CancelResult {
    CancelStatus status;
    Quantity releasedQuantity = 0;
};
struct Commitments {
    std::uint64_t cash = 0;
    Quantity shares = 0;
    Quantity buys = 0;
    Quantity sells = 0;
};
}
