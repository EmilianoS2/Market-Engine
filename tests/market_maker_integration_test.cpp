#include "market_engine/market_maker.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace market_engine;
struct Fixture {
    MarketMakerConfig config;
    OrderBook book;
    SimulationAccount account;
    ReferencePrice reference;
    EngineAdapter adapter;
    MarketMaker maker;
    CandleBuilder candles;
    std::vector<Execution> executions;
    Fixture(MarketMakerConfig c = {}) : config(c), account(c.owner, c.initialCash, c.initialInventory), reference(c.referencePrice), adapter(book, account, c), maker(c, account, adapter, reference) { maker.start(simulatedTime(0)); }
    SubmitResult submit(OrderId id, Side side, Price price, Quantity quantity, std::string owner = "TRADER", std::uint64_t time = 10) {
        auto result = adapter.submit({{id, side, price, quantity}, owner, config.symbol}, simulatedTime(time));
        if (result.accepted()) {
            maker.onExecutions(result.executions, simulatedTime(time));
            for (const auto &execution : result.executions) { candles.addTrade(execution.trade); executions.push_back(execution); }
        }
        return result;
    }
    OpenOrder quote(Side side) {
        for (const auto &open : adapter.openOrders(config.owner)) if (open.order.side == side) return open;
        throw std::runtime_error("No quote on requested side.");
    }
};
int main() {
    { Fixture f; assert(f.quote(Side::Buy).order.price == 9750 && f.quote(Side::Sell).order.price == 10250);
      assert(f.account.cash() == 1000000 && f.account.inventory() == 100);
      auto result = f.submit(1, Side::Buy, 10300, 3); assert(result.accepted() && result.executions.size() == 1);
      assert(result.executions[0].seller == f.config.owner && result.executions[0].trade.price == 10250);
      assert(f.account.cash() == 1030750 && f.account.inventory() == 97 && f.quote(Side::Sell).order.remaining == 2);
      assert(f.candles.candles()[0].volume == 3 && f.candles.candles()[0].open == 10250);
      f.maker.onExecutions(result.executions, simulatedTime(10)); assert(f.account.cash() == 1030750 && f.maker.metrics().sellFills == 1);
      auto id = f.quote(Side::Sell).order.id; auto cancel = f.adapter.cancel(id, f.config.owner); assert(cancel.releasedQuantity == 2);
      assert(f.account.cash() == 1030750 && f.account.inventory() == 97 && f.account.commitments(f.adapter.openOrders()).shares == 0); }
    { Fixture f; f.submit(1, Side::Sell, 9700, 2); assert(f.account.cash() == 980500 && f.account.inventory() == 102 && f.quote(Side::Buy).order.remaining == 3); }
    { auto c = MarketMakerConfig{}; c.skewBpsPerShare = 0; Fixture f(c);
      auto bid = f.quote(Side::Buy); auto ask = f.quote(Side::Sell);
      f.maker.advance(simulatedTime(1000)); assert(f.quote(Side::Buy).order.id == bid.order.id && f.quote(Side::Sell).order.id == ask.order.id);
      f.submit(1, Side::Buy, 10300, 3, "BUYER", 1100); f.maker.advance(simulatedTime(2000));
      assert(f.quote(Side::Sell).order.id == ask.order.id && f.quote(Side::Sell).order.remaining == 2);
      f.submit(2, Side::Buy, 9750, 4, "QUEUE", 2100); f.submit(3, Side::Sell, 9750, 1, "SELLER", 2200);
      assert(f.executions.back().trade.makerId == bid.order.id); }
    { Fixture f; auto previous = f.adapter.openOrders(f.config.owner); auto c = f.config; c.referencePrice = 11000;
      f.maker.configure(c, simulatedTime(1000)); auto replaced = f.adapter.openOrders(f.config.owner); assert(replaced.size() == 2);
      for (const auto &old : previous) assert(!f.book.contains(old.order.id));
      assert(f.maker.metrics().pnl == 100000 && f.maker.metrics().excessReturn == 0);
      auto bad = c; bad.referencePrice = UINT64_MAX; bool rejected = false;
      try { f.maker.configure(bad, simulatedTime(1000)); } catch (const std::exception &) { rejected = true; }
      assert(rejected && f.reference.value() == 11000 && f.adapter.openOrders(f.config.owner)[0].order.id == replaced[0].order.id); }
    { Fixture f; auto before = f.adapter.openOrders(); auto cash = f.account.cash();
      auto rejected = f.adapter.submit({{99, Side::Buy, 10300, 1}, f.config.owner, f.config.symbol, true}, simulatedTime(10));
      assert(!rejected.accepted() && f.adapter.openOrders().size() == before.size() && f.account.cash() == cash && !f.book.contains(99));
      assert(f.adapter.cancel(before[0].order.id, "TRADER").status == CancelStatus::NotOwner); }
    { auto c = MarketMakerConfig{}; c.initialCash = 20000; Fixture f(c); assert(f.quote(Side::Buy).order.remaining == 2); }
    { auto c = MarketMakerConfig{}; c.initialInventory = c.targetInventory = 2; Fixture f(c); assert(f.quote(Side::Sell).order.remaining == 2); }
    { auto c = MarketMakerConfig{}; c.initialInventory = 150; Fixture f(c); assert(f.adapter.openOrders(c.owner).size() == 1 && f.maker.buyDisabled());
      f.maker.advance(simulatedTime(1000)); assert(f.maker.metrics().buyDisabledMs == 1000); }
    { auto c = MarketMakerConfig{}; c.initialInventory = 0; Fixture f(c); assert(f.adapter.openOrders(c.owner).size() == 1 && f.maker.sellDisabled()); }
    { auto c = MarketMakerConfig{}; c.initialInventory = 149; Fixture f(c); assert(f.quote(Side::Buy).order.remaining == 1); }
    { Fixture f; f.submit(1, Side::Sell, 10200, 2, "OTHER_SELLER");
      auto result = f.submit(2, Side::Buy, 10300, 10, "BUYER"); assert(result.executions.size() == 2);
      assert(result.executions[0].sequence < result.executions[1].sequence && result.executions[0].seller == "OTHER_SELLER");
      assert(f.account.cash() == 1051250 && f.account.inventory() == 95 && f.candles.candles()[0].volume == 7);
      f.maker.stop(simulatedTime(20)); assert(f.adapter.openOrders(f.config.owner).empty() && f.book.contains(2)); assert(f.account.inventory() == 95);
      assert(f.adapter.cancel(2, "BUYER").status == CancelStatus::Cancelled && f.adapter.cancel(2, "BUYER").status == CancelStatus::NotOpen); }
    { Fixture f; f.submit(1, Side::Buy, 9500, 1, "SELF"); auto result = f.submit(2, Side::Sell, 9500, 1, "SELF"); assert(!result.accepted() && f.book.contains(1) && !f.book.contains(2)); }
    { Fixture f; f.submit(1, Side::Buy, 10300, 5); assert(f.adapter.openOrders(f.config.owner).size() == 1);
      f.maker.advance(simulatedTime(1000)); assert(f.quote(Side::Sell).order.remaining == 5 && f.adapter.openOrders(f.config.owner).size() == 2); }
    { Fixture f; const auto previous = f.quote(Side::Buy).order.id; auto bad = f.config; bad.refreshIntervalMs = UINT64_MAX;
      bool rejected = false; try { f.maker.configure(bad, simulatedTime(10)); } catch (const std::exception &) { rejected = true; }
      assert(rejected && f.book.contains(previous));
      f.reference.set(UINT64_MAX); f.maker.advance(simulatedTime(1000)); assert(f.maker.state() == MakerState::Faulted && f.adapter.openOrders(f.config.owner).empty());
      assert(f.account.cash() == 1000000 && f.account.inventory() == 100);
      f.maker.reset(simulatedTime(1000)); assert(f.maker.state() == MakerState::Stopped && f.reference.value() == 10000); }
    auto deterministic = [] {
        Fixture f; f.submit(1, Side::Buy, 10300, 3); f.maker.advance(simulatedTime(1000));
        f.submit(2, Side::Sell, 9700, 2, "TRADER", 61000); f.maker.advance(simulatedTime(62000)); f.maker.stop(simulatedTime(63000));
        std::ostringstream out;
        for (const auto &e : f.executions) out << e.id << ':' << e.trade.makerId << ':' << e.trade.takerId << ':' << milliseconds(e.trade.ExecutedAt) << ':' << e.trade.price << ':' << e.trade.quantity << ':' << e.buyer << ':' << e.seller << '\n';
        for (const auto &c : f.candles.candles()) out << milliseconds(c.startTime) << ':' << c.open << ':' << c.high << ':' << c.low << ':' << c.close << ':' << c.volume << '\n';
        const auto &m = f.maker.metrics(); out << f.account.cash() << ':' << f.account.inventory() << ':' << m.pnl << ':' << m.excessReturn << ':' << m.maximumDrawdown << ':' << m.buyFills << ':' << m.sellFills;
        assert(f.candles.candles().size() == 2); return out.str();
    };
    assert(deterministic() == deterministic());
    std::cout << "PASS integration: attributed fills, FIFO, partial remainders, risk limits, replacement, post-only, isolation, deterministic candles/metrics, shutdown\n";
}
