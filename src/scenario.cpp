#include "market_engine/scenario.hpp"
#include "market_engine/checked_math.hpp"
#include <charconv>

namespace market_engine {
namespace {
std::uint64_t unsignedField(const std::string &field) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
    if (result.ec != std::errc{} || result.ptr != field.data() + field.size()) throw std::invalid_argument("Invalid scenario integer.");
    return value;
}
}
std::vector<ScenarioEvent> readScenario(const std::string &filename) {
    std::ifstream file(filename); if (!file) throw std::runtime_error("Cannot open scenario: " + filename);
    std::string line; std::getline(file, line);
    if (line != "time_ms,phase,action,owner,order_id,side,price_ticks,quantity") throw std::invalid_argument("Invalid scenario CSV header.");
    std::vector<ScenarioEvent> events;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream input(line); std::vector<std::string> fields;
        for (std::string field; std::getline(input, field, ',');) fields.push_back(field);
        if (fields.size() != 8) throw std::invalid_argument("Scenario rows must have eight fields.");
        ScenarioEvent event{unsignedField(fields[0]), fields[1], fields[2], fields[3], {unsignedField(fields[4]), Side::Buy, unsignedField(fields[6]), unsignedField(fields[7])}};
        simulatedTime(event.timeMs);
        if (!events.empty() && event.timeMs < events.back().timeMs) throw std::invalid_argument("Scenario time must be ordered.");
        if (event.action == "submit") {
            if (fields[5] != "buy" && fields[5] != "sell") throw std::invalid_argument("Invalid scenario side.");
            event.order.side = fields[5] == "buy" ? Side::Buy : Side::Sell;
            if (event.owner.empty() || !event.order.id || !event.order.price || !event.order.remaining) throw std::invalid_argument("Invalid scenario order.");
        } else if (event.action != "reference" && event.action != "cancel" && event.action != "end") throw std::invalid_argument("Invalid scenario action.");
        events.push_back(std::move(event));
    }
    if (events.empty()) throw std::invalid_argument("Scenario has no events.");
    return events;
}
ScenarioResult runScenario(const MarketMakerConfig &config, const std::vector<ScenarioEvent> &events, TradeCSVWriter *writer) {
    config.validate();
    OrderBook book; SimulationAccount account(config.owner, config.initialCash, config.initialInventory);
    ReferencePrice reference(config.referencePrice); EngineAdapter adapter(book, account, config);
    MarketMaker maker(config, account, adapter, reference); CandleBuilder candles;
    ScenarioResult result{}; std::ostringstream measurements;
    measurements << "time_ms,bid_ticks,bid_quantity,ask_ticks,ask_quantity,reference_ticks,inventory,cash_ticks,reserved_cash_ticks,reserved_shares,buy_fills,sell_fills,buy_volume,sell_volume,rejections,buy_disabled_ms,sell_disabled_ms,portfolio_value_ticks,pnl_ticks,max_drawdown_ticks,min_inventory,max_inventory,excess_return_ticks\n";
    auto record = [&](std::uint64_t time) {
        maker.measure(simulatedTime(time)); const auto &m = maker.metrics();
        auto orders = adapter.openOrders(config.owner); auto reserved = account.commitments(orders);
        Price bid = 0, ask = 0; Quantity bidQuantity = 0, askQuantity = 0;
        for (const auto &open : orders) {
            if (open.order.side == Side::Buy) { bid = open.order.price; bidQuantity = open.order.remaining; }
            else { ask = open.order.price; askQuantity = open.order.remaining; }
        }
        measurements << time << ',' << bid << ',' << bidQuantity << ',' << ask << ',' << askQuantity << ',' << reference.value()
            << ',' << account.inventory() << ',' << account.cash() << ',' << reserved.cash << ',' << reserved.shares
            << ',' << m.buyFills << ',' << m.sellFills << ',' << m.buyVolume << ',' << m.sellVolume << ',' << m.rejections
            << ',' << m.buyDisabledMs << ',' << m.sellDisabledMs << ',' << m.portfolioValue << ',' << m.pnl << ',' << m.maximumDrawdown
            << ',' << m.minimumInventory << ',' << m.maximumInventory << ',' << m.excessReturn << '\n';
    };
    bool started = false;
    for (std::size_t index = 0; index < events.size();) {
        const auto time = events[index].timeMs;
        while (started && maker.nextDecisionMs() < time) {
            auto due = maker.nextDecisionMs(); maker.advance(simulatedTime(due)); record(due);
            if (maker.state() != MakerState::Running) throw std::runtime_error(maker.fault());
        }
        std::size_t end = index;
        while (end < events.size() && events[end].timeMs == time) ++end;
        // Reference updates precede due decisions, then counterparties in CSV order.
        for (std::size_t i = index; i < end; ++i) if (events[i].action == "reference") {
            account.portfolioValue(events[i].order.price);
            calculateQuotes(config, events[i].order.price, account.inventory());
            reference.set(events[i].order.price);
        }
        if (!started) { maker.start(simulatedTime(time)); started = true; }
        else maker.advance(simulatedTime(time));
        record(time);
        for (std::size_t i = index; i < end; ++i) {
            const auto &event = events[i];
            if (event.action == "submit") {
                if (event.owner == config.owner) throw std::invalid_argument("Scenario counterparties cannot impersonate the bot.");
                auto submitted = adapter.submit({event.order, event.owner, config.symbol}, simulatedTime(time));
                if (!submitted.accepted()) throw std::runtime_error(submitted.error);
                maker.onExecutions(submitted.executions, simulatedTime(time));
                for (const auto &execution : submitted.executions) {
                    if (writer) writer->writeTrade(execution.trade);
                    candles.addTrade(execution.trade); result.executions.push_back(execution);
                }
            } else if (event.action == "cancel") {
                if (event.owner == config.owner || adapter.cancel(event.order.id, event.owner).status == CancelStatus::NotOwner)
                    throw std::invalid_argument("Scenario cancellation owner mismatch.");
            }
            record(time);
            if (maker.state() == MakerState::Faulted) throw std::runtime_error(maker.fault());
        }
        index = end;
    }
    const auto endTime = checkedAdd(events.back().timeMs, config.refreshIntervalMs);
    maker.stop(simulatedTime(endTime)); record(endTime);
    if (!adapter.openOrders(config.owner).empty()) throw std::logic_error("Scenario left bot orders open.");
    result.candles = candles.candles(); result.metrics = maker.metrics(); result.cash = account.cash(); result.inventory = account.inventory();
    result.measurementsCsv = measurements.str(); return result;
}
}
