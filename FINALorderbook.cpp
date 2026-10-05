#include <cstdint>
#include <vector>
#include <deque>
#include <functional>
#include <map>
#include <unordered_map>
#include <cassert>
#include <stdexcept>
#include <algorithm>

using OrderId = std::uint64_t;
using Price = std::uint64_t;
using Quantity = std::uint64_t;

enum class Side
{
    Buy,
    Sell
};

// Represents an order that may still be waiting.
struct Order
{
    OrderId id;
    Side side;
    Price price;
    Quantity remaining;
};

// Represents a comleted matched order.
struct Trade
{
    OrderId makerId; // Resting Order
    OrderId takerId; // Incoming Order
    Price price;
    Quantity quantity;
};

class OrderBook
{
public:
    std::vector<Trade> addOrder(Order order);
    bool cancelOrder(OrderId id);

    bool contains(OrderId id) const;
    Quantity quantityAt(Side side, Price price) const;
    std::size_t orderCountAt(Side side, Price price) const;

private:
    using OrderQueue = std::deque<OrderId>;
    using BidBook = std::map<Price, OrderQueue, std::greater<Price>>;
    using AskBook = std::map<Price, OrderQueue>;
    std::unordered_map<OrderId, Order> activeOrders_;
    BidBook bids_;
    AskBook asks_;
};

// If True order exists, if False order does not exist.
bool OrderBook::contains(OrderId id) const
{
    return activeOrders_.find(id) != activeOrders_.end();
}

std::vector<Trade> OrderBook::addOrder(Order order)
{
    if (order.id == 0 || order.price == 0 || order.remaining == 0)
    {
        throw std::invalid_argument("Order values must be greater than zero.");
    }
    if (contains(order.id))
    {
        throw std::invalid_argument("Order with this ID already exists.");
    }

    std::vector<Trade> trades;

    if (order.side == Side::Buy)
    {
        auto it = asks_.begin();
        while (it != asks_.end() && order.remaining > 0 && it->first <= order.price)
        {
            auto &queue = it->second;
            while (!queue.empty() && order.remaining > 0)
            {
                OrderId makerId = queue.front();
                Order &makerOrder = activeOrders_.at(makerId);
                Quantity tradeQuantity = std::min(order.remaining, makerOrder.remaining);
                order.remaining -= tradeQuantity;
                makerOrder.remaining -= tradeQuantity;

                if (makerOrder.remaining == 0)
                {
                    queue.pop_front();
                    activeOrders_.erase(makerId);
                }

                trades.push_back({makerId, order.id, it->first, tradeQuantity});
            }
            if (queue.empty())
            {
                it = asks_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    else // Side::Sell
    {
        auto it = bids_.begin();
        while (it != bids_.end() && order.remaining > 0 && it->first >= order.price)
        {
            auto &queue = it->second;
            while (!queue.empty() && order.remaining > 0)
            {
                OrderId makerId = queue.front();
                Order &makerOrder = activeOrders_.at(makerId);
                Quantity tradeQuantity = std::min(order.remaining, makerOrder.remaining);
                order.remaining -= tradeQuantity;
                makerOrder.remaining -= tradeQuantity;

                if (makerOrder.remaining == 0)
                {
                    queue.pop_front();
                    activeOrders_.erase(makerId);
                }

                trades.push_back({makerId, order.id, it->first, tradeQuantity});
            }
            if (queue.empty())
            {
                it = bids_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    if (order.remaining > 0)
    {
        activeOrders_.emplace(order.id, order);
        if (order.side == Side::Buy)
        {
            bids_[order.price].push_back(order.id);
        }
        else
        {
            asks_[order.price].push_back(order.id);
        }
    }
    return trades;
}

Quantity OrderBook::quantityAt(Side side, Price price) const
{
    Quantity total = 0;

    if (side == Side::Buy)
    {
        auto level = bids_.find(price);
        if (level == bids_.end())
        {
            return 0;
        }

        for (OrderId id : level->second)
        {
            total += activeOrders_.at(id).remaining;
        }
    }
    else
    {
        auto level = asks_.find(price);

        if (level == asks_.end())
        {
            return 0;
        }
        for (OrderId id : level->second)
        {
            total += activeOrders_.at(id).remaining;
        }
    }
    return total;
}

std::size_t OrderBook::orderCountAt(Side side, Price price) const
{
    if (side == Side::Buy)
    {
        auto level = bids_.find(price);
        if (level == bids_.end())
        {
            return 0;
        }
        return level->second.size();
    }
    else
    {
        auto level = asks_.find(price);
        if (level == asks_.end())
        {
            return 0;
        }
        return level->second.size();
    }
}

bool OrderBook::cancelOrder(OrderId id)
{
    auto it = activeOrders_.find(id);
    if (it == activeOrders_.end())
    {
        return false; // Order not found
    }
    else
    {
        Order order = it->second;
        activeOrders_.erase(it);

        if (order.side == Side::Buy)
        {
            auto level = bids_.find(order.price);
            if (level != bids_.end())
            {
                auto &queue = level->second;
                queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
                if (queue.empty())
                {
                    bids_.erase(level);
                }
            }
        }
        else
        {
            auto level = asks_.find(order.price);
            if (level != asks_.end())
            {
                auto &queue = level->second;
                queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
                if (queue.empty())
                {
                    asks_.erase(level);
                }
            }
        }
        return true; // Order successfully canceled
    }
}

#ifndef ORDERBOOK_LIBRARY_ONLY
void testAddAndCancel()
{
    OrderBook book;

    assert(!book.contains(2));

    std::vector<Trade> trades = book.addOrder({2, Side::Buy, 1000, 5});

    assert(trades.empty());
    assert(book.contains(2));
    assert(book.orderCountAt(Side::Buy, 1000) == 1);
    assert(book.orderCountAt(Side::Buy, 2000) == 0);
    assert(book.orderCountAt(Side::Sell, 1000) == 0);
    assert(!book.cancelOrder(999)); // Unknown ID
    assert(book.cancelOrder(2));    // Existing order
    assert(!book.contains(2));
    assert(book.orderCountAt(Side::Buy, 1000) == 0);
    assert(book.quantityAt(Side::Buy, 1000) == 0);
}

void testCancelOneOfTwo()
{
    OrderBook secondBook;
    secondBook.addOrder({10, Side::Buy, 1000, 5});
    secondBook.addOrder({11, Side::Buy, 1000, 7});

    assert(secondBook.cancelOrder(10));
    assert(secondBook.orderCountAt(Side::Buy, 1000) == 1);
    assert(secondBook.quantityAt(Side::Buy, 1000) == 7);
}

void testBuyMatch()
{
    OrderBook matchingBook;
    matchingBook.addOrder({20, Side::Sell, 1000, 5});
    auto matches = matchingBook.addOrder({21, Side::Buy, 1000, 3});

    assert(matches.size() == 1);
    assert(matches[0].makerId == 20);
    assert(matches[0].takerId == 21);
    assert(matches[0].price == 1000);
    assert(matches[0].quantity == 3);
    assert(matchingBook.quantityAt(Side::Sell, 1000) == 2);
    assert(!matchingBook.contains(21));
}

void testSellMatch()
{
    OrderBook sellMatchBook;
    sellMatchBook.addOrder({30, Side::Buy, 1050, 4});
    auto sellTrades = sellMatchBook.addOrder({31, Side::Sell, 1000, 6});

    assert(sellTrades.size() == 1);
    assert(sellTrades[0].makerId == 30);
    assert(sellTrades[0].takerId == 31);
    assert(sellTrades[0].price == 1050);
    assert(sellTrades[0].quantity == 4);
    assert(!sellMatchBook.contains(30));
    assert(sellMatchBook.contains(31));
    assert(sellMatchBook.quantityAt(Side::Sell, 1000) == 2);
}

void testPricePriority()
{
    OrderBook priorityBook;
    priorityBook.addOrder({40, Side::Sell, 1010, 2});
    priorityBook.addOrder({41, Side::Sell, 1000, 3});
    priorityBook.addOrder({42, Side::Sell, 1000, 4});

    auto priorityTrades = priorityBook.addOrder({43, Side::Buy, 1010, 8});
    assert(priorityTrades.size() == 3);
    assert(priorityTrades[0].makerId == 41);
    assert(priorityTrades[1].makerId == 42);
    assert(priorityTrades[2].makerId == 40);
    assert(priorityBook.quantityAt(Side::Sell, 1010) == 1);
}

void testNonCrossingOrders()
{
    OrderBook spreadBook;
    spreadBook.addOrder({50, Side::Sell, 1100, 5});
    auto noTrades = spreadBook.addOrder({51, Side::Buy, 1000, 3});
    assert(noTrades.empty());
    assert(spreadBook.contains(50));
    assert(spreadBook.contains(51));
    assert(spreadBook.quantityAt(Side::Sell, 1100) == 5);
    assert(spreadBook.quantityAt(Side::Buy, 1000) == 3);
}

void testDuplicateId()
{
    OrderBook duplicateBook;
    duplicateBook.addOrder({60, Side::Buy, 1000, 4});

    bool rejected = false;
    try
    {
        duplicateBook.addOrder({60, Side::Sell, 1100, 2});
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }

    assert(rejected);
    assert(duplicateBook.quantityAt(Side::Buy, 1000) == 4);
    assert(duplicateBook.quantityAt(Side::Sell, 1100) == 0);
}

int main()
{
    testAddAndCancel();
    testCancelOneOfTwo();
    testBuyMatch();
    testSellMatch();
    testPricePriority();
    testNonCrossingOrders();
    testDuplicateId();
}
#endif