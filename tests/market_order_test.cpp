#include "market_engine/market_maker.hpp"
#include <cassert>
#include <iostream>
using namespace market_engine;

struct Fixture {
    MarketMakerConfig config;
    OrderBook book;
    SimulationAccount account{config.owner, config.initialCash, config.initialInventory};
    ReferencePrice reference{config.referencePrice};
    EngineAdapter adapter{book, account, config};
    MarketMaker maker{config, account, adapter, reference};
    CandleBuilder candles;
    SubmitResult submit(Order order, std::string owner = "TRADER", bool postOnly = false) {
        auto result = adapter.submit({order, owner, config.symbol, postOnly}, simulatedTime(10));
        if (result.accepted()) {
            maker.onExecutions(result.executions, simulatedTime(10));
            for (const auto &execution : result.executions) candles.addTrade(execution.trade);
        }
        return result;
    }
};

int main() {
    { OrderBook book;
      book.addOrder({1, Side::Sell, 110, 2}); book.addOrder({2, Side::Sell, 100, 2}); book.addOrder({3, Side::Sell, 100, 3});
      auto trades = book.addOrder({4, Side::Buy, 0, 6, OrderType::Market});
      assert(trades.size() == 3 && trades[0].makerId == 2 && trades[1].makerId == 3 && trades[2].makerId == 1);
      assert(trades[0].price == 100 && trades[1].price == 100 && trades[2].price == 110);
      assert(trades[0].quantity == 2 && trades[1].quantity == 3 && trades[2].quantity == 1);
      assert(!book.contains(4) && book.quantityAt(Side::Sell, 110) == 1 && book.orderCountAt(Side::Buy, 0) == 0); }
    { OrderBook book;
      book.addOrder({1, Side::Buy, 90, 2}); book.addOrder({2, Side::Buy, 100, 2}); book.addOrder({3, Side::Buy, 100, 3});
      auto trades = book.addOrder({4, Side::Sell, 0, 6, OrderType::Market});
      assert(trades.size() == 3 && trades[0].makerId == 2 && trades[1].makerId == 3 && trades[2].makerId == 1);
      assert(trades[0].price == 100 && trades[2].price == 90 && !book.contains(4)); }
    { OrderBook book;
      assert(book.addOrder({1, Side::Buy, 0, 5, OrderType::Market}).empty());
      assert(book.addOrder({2, Side::Sell, 0, 5, OrderType::Market}).empty());
      assert(!book.contains(1) && !book.contains(2));
      book.addOrder({3, Side::Sell, UINT64_MAX, 2});
      auto trades = book.addOrder({4, Side::Buy, 0, UINT64_MAX, OrderType::Market});
      assert(trades.size() == 1 && trades[0].price == UINT64_MAX && !book.contains(4)); }
    { Fixture f;
      f.submit({1, Side::Sell, 100, 2}, "SELLER");
      auto result = f.submit({2, Side::Buy, 0, 5, OrderType::Market}, "BUYER");
      assert(result.status == SubmitStatus::PartiallyFilledCancelled && result.filledQuantity == 2 && result.cancelledQuantity == 3 && result.remaining == 0);
      assert(!f.book.contains(2) && f.adapter.openOrders().empty());
      assert(result.executions[0].buyer == "BUYER" && result.executions[0].seller == "SELLER");
      assert(f.candles.candles()[0].volume == 2 && f.candles.candles()[0].close == 100);
      auto empty = f.submit({3, Side::Sell, 0, 7, OrderType::Market}, "SELLER");
      assert(empty.status == SubmitStatus::Cancelled && empty.cancelledQuantity == 7 && empty.filledQuantity == 0 && empty.executions.empty()); }
    { Fixture f; f.maker.start(simulatedTime(0));
      auto result = f.submit({1, Side::Buy, 0, 3, OrderType::Market});
      assert(result.status == SubmitStatus::Filled && result.filledQuantity == 3 && result.cancelledQuantity == 0);
      assert(result.executions[0].trade.price == 10250 && result.executions[0].seller == "MM_LUFFY");
      assert(f.account.cash() == 1030750 && f.account.inventory() == 97);
      assert(f.account.commitments(f.adapter.openOrders()).shares == 2 && f.candles.candles()[0].volume == 3); }
    { Fixture f; f.maker.start(simulatedTime(0));
      auto result = f.submit({1, Side::Sell, 0, 2, OrderType::Market});
      assert(result.status == SubmitStatus::Filled && result.executions[0].trade.price == 9750);
      assert(f.account.cash() == 980500 && f.account.inventory() == 102 && f.account.commitments(f.adapter.openOrders()).buys == 3); }
    { Fixture f; f.maker.start(simulatedTime(0));
      auto result = f.submit({1, Side::Buy, 0, 8, OrderType::Market});
      assert(result.filledQuantity == 5 && result.cancelledQuantity == 3 && f.account.inventory() == 95);
      assert(f.candles.candles()[0].volume == 5 && !f.book.contains(1)); }
    { Fixture f;
      f.submit({1, Side::Sell, 100, 2}, "OTHER"); f.submit({2, Side::Sell, 110, 3}, "SELF");
      auto rejected = f.submit({3, Side::Buy, 0, 4, OrderType::Market}, "SELF");
      assert(!rejected.accepted() && f.book.quantityAt(Side::Sell, 100) == 2 && f.book.quantityAt(Side::Sell, 110) == 3);
      assert(f.candles.candles().empty());
      auto allowed = f.submit({4, Side::Buy, 0, 2, OrderType::Market}, "SELF");
      assert(allowed.status == SubmitStatus::Filled && f.book.contains(2)); }
    { Fixture f;
      assert(!f.submit({1, Side::Buy, 0, 1, OrderType::Market}, "TRADER", true).accepted());
      assert(!f.submit({2, Side::Buy, 0, 1, OrderType::Market}, f.config.owner).accepted());
      assert(!f.submit({3, Side::Buy, 100, 1, OrderType::Market}).accepted());
      assert(!f.submit({4, Side::Buy, 0, 0, OrderType::Market}).accepted());
      assert(!f.submit({5, Side::Buy, 0, 1, static_cast<OrderType>(99)}).accepted());
      assert(f.adapter.openOrders().empty() && f.account.cash() == 1000000);
      auto limit = f.submit({6, Side::Buy, 100, 1}); assert(limit.status == SubmitStatus::Resting && f.book.contains(6)); }
    std::cout << "PASS market orders: best price/FIFO, multi-level sweeps, full/partial/empty fills, no resting remainder, attribution/account/candles, self-trade prevention, invalid/post-only rejection\n";
}
