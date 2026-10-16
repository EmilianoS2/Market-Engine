#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <functional>
#include <map>
#include <unordered_map>
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <sstream>

#include <optional>

using OrderId = std::uint64_t;
using Price = std::uint64_t;
using Quantity = std::uint64_t;
using Timestamp = std::chrono::system_clock::time_point;

enum class Side
{
    Buy,
    Sell
};

enum class OrderType
{
    Limit,
    Market
};

// Incoming orders; only limit orders can remain in the book.
struct Order
{
    OrderId id;
    Side side;
    Price price; // Limit price in ticks; zero for a market order.
    Quantity remaining;
    OrderType type = OrderType::Limit;
};

// Represents a comleted matched order.
struct Trade
{
    OrderId makerId; // Resting Order
    OrderId takerId; // Incoming Order
    Price price;
    Quantity quantity;
    Timestamp ExecutedAt;
};

struct Candle
{
    Timestamp startTime; // Start of the minute
    Price open;
    Price high;
    Price low;
    Price close;
    Quantity volume;
};

class CandleBuilder
{
public:
    void addTrade(const Trade &trade);

    const std::vector<Candle> &candles() const
    {
        return candles_;
    }

private:
    std::vector<Candle> candles_;
};

class OrderBook
{
public:
    std::vector<Trade> addOrder(Order order, Timestamp executedAt = std::chrono::system_clock::now());
    std::optional<Order> findOrder(OrderId id) const;
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

class TradeCSVWriter
{
public:
    void writeTrade(const Trade &trade)
    {
        file_ << trade.makerId << ","
              << trade.takerId << ","
              << trade.price << ","
              << trade.quantity << ","
              << std::chrono::duration_cast<std::chrono::milliseconds>(trade.ExecutedAt.time_since_epoch()).count()
              << "\n";
        file_.flush();
        if (!file_) throw std::runtime_error("Failed to write trade CSV.");
    }
    explicit TradeCSVWriter(const std::string &filename) : file_(filename, std::ios::app)
    {
        if (!file_.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        file_.seekp(0, std::ios::end);
        if (file_.tellp() == std::streampos(0))
        {
            file_ << "MakerId,TakerId,Price,Quantity,ExecutedAtMs\n";
        }
    }

private:
    std::ofstream file_;
};

class TradeCSVReader
{
public:
    explicit TradeCSVReader(const std::string &filename)
        : file_(filename)
    {
        if (!file_)
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }
    }

    std::vector<Trade> readTrades();

private:
    std::ifstream file_;
};

