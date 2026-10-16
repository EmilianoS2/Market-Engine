#include "include/market_engine/order_book.hpp"

void CandleBuilder::addTrade(const Trade &trade)
{
    Timestamp minuteStart =
        std::chrono::floor<std::chrono::minutes>(trade.ExecutedAt);

    if (candles_.empty() || candles_.back().startTime != minuteStart)
    {
        candles_.push_back({minuteStart,
                            trade.price, trade.price, trade.price, trade.price,
                            trade.quantity});
        return;
    }

    Candle &current = candles_.back();
    current.high = std::max(current.high, trade.price);
    current.low = std::min(current.low, trade.price);
    current.close = trade.price;
    current.volume += trade.quantity;
}

std::vector<Trade> TradeCSVReader::readTrades()
{
    std::vector<Trade> trades;
    std::string line;

    std::getline(file_, line); // Skip the header

    while (std::getline(file_, line))
    {
        std::istringstream row(line);
        std::string makerText, takerText, priceText, quantityText, timeText;

        if (!std::getline(row, makerText, ',') ||
            !std::getline(row, takerText, ',') ||
            !std::getline(row, priceText, ',') ||
            !std::getline(row, quantityText, ',') ||
            !std::getline(row, timeText))
        {
            throw std::runtime_error("Invalid trade CSV row");
        }
        Trade trade{
            std::stoull(makerText),
            std::stoull(takerText),
            std::stoull(priceText),
            std::stoull(quantityText),
            Timestamp{std::chrono::milliseconds{std::stoll(timeText)}}};

        trades.push_back(trade);
    }

    return trades;
}

// If True order exists, if False order does not exist.
bool OrderBook::contains(OrderId id) const
{
    return activeOrders_.find(id) != activeOrders_.end();
}

std::optional<Order> OrderBook::findOrder(OrderId id) const
{
    auto found = activeOrders_.find(id);
    if (found == activeOrders_.end()) return std::nullopt;
    return found->second;
}

std::vector<Trade> OrderBook::addOrder(Order order, Timestamp executedAt)
{
    if ((order.type != OrderType::Limit && order.type != OrderType::Market) ||
        (order.side != Side::Buy && order.side != Side::Sell))
    {
        throw std::invalid_argument("Invalid order type or side.");
    }
    const bool market = order.type == OrderType::Market;
    if (order.id == 0 || order.remaining == 0 || (!market && order.price == 0))
    {
        throw std::invalid_argument("Order values must be greater than zero.");
    }
    if (market && order.price != 0)
    {
        throw std::invalid_argument("Market orders do not take a limit price.");
    }
    if (contains(order.id))
    {
        throw std::invalid_argument("Order with this ID already exists.");
    }

    std::vector<Trade> trades;

    if (order.side == Side::Buy)
    {
        auto it = asks_.begin();
        while (it != asks_.end() && order.remaining > 0 && (market || it->first <= order.price))
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
                trades.push_back({makerId, order.id, it->first, tradeQuantity, executedAt});
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
        while (it != bids_.end() && order.remaining > 0 && (market || it->first >= order.price))
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

                trades.push_back({makerId, order.id, it->first, tradeQuantity, executedAt});
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
    // Market orders consume available liquidity immediately; unfilled quantity expires.
    if (order.remaining > 0 && !market)
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

void testCandleBuilder()
{
    CandleBuilder builder;
    Timestamp now = std::chrono::system_clock::now();
    Timestamp minuteStart = std::chrono::floor<std::chrono::minutes>(now);

    builder.addTrade({1, 2, 1000, 5, minuteStart + std::chrono::seconds(10)});
    builder.addTrade({3, 4, 1010, 3, minuteStart + std::chrono::seconds(20)});
    builder.addTrade({5, 6, 990, 2, minuteStart + std::chrono::seconds(30)});

    const auto &candles = builder.candles();
    assert(candles.size() == 1);
    assert(candles[0].startTime == minuteStart);
    assert(candles[0].open == 1000);
    assert(candles[0].high == 1010);
    assert(candles[0].low == 990);
    assert(candles[0].close == 990);
    assert(candles[0].volume == 10);

    // Add a trade in the next minute
    builder.addTrade({7, 8, 1020, 4, minuteStart + std::chrono::minutes(1) + std::chrono::seconds(5)});
    const auto &updatedCandles = builder.candles();
    assert(updatedCandles.size() == 2);
    assert(updatedCandles[1].startTime == minuteStart + std::chrono::minutes(1));
    assert(updatedCandles[1].open == 1020);
    assert(updatedCandles[1].high == 1020);
    assert(updatedCandles[1].low == 1020);
    assert(updatedCandles[1].close == 1020);
    assert(updatedCandles[1].volume == 4);
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
    testCandleBuilder();
}
#endif
