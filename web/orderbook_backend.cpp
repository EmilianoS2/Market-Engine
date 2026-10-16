#include "market_engine/market_maker.hpp"
#include "market_engine/checked_math.hpp"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace market_engine;
namespace {
bool parseUnsigned(const std::string &text, std::uint64_t &value) {
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
void quoted(std::ostream &out, const std::string &text) {
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else out << c;
    }
    out << '"';
}
template<class T> void number(std::ostream &out, const char *name, T value) {
    out << ','; quoted(out, name); out << ":\"" << value << '"';
}
void writeTrade(std::ostream &out, const Execution &e) {
    out << "{\"makerId\":\"" << e.trade.makerId << '"';
    number(out, "takerId", e.trade.takerId); number(out, "price", e.trade.price);
    number(out, "quantity", e.trade.quantity); number(out, "tradeId", e.id);
    number(out, "sequence", e.sequence); number(out, "executedAtMs", milliseconds(e.trade.ExecutedAt));
    number(out, "buyOrderId", e.buyOrderId); number(out, "sellOrderId", e.sellOrderId);
    out << ",\"buyer\":"; quoted(out, e.buyer); out << ",\"seller\":"; quoted(out, e.seller);
    out << ",\"symbol\":"; quoted(out, e.symbol); out << '}';
}
void writeCandle(std::ostream &out, const Candle &c) {
    out << "{\"startTimeMs\":\"" << milliseconds(c.startTime) << '"';
    number(out, "open", c.open); number(out, "high", c.high); number(out, "low", c.low);
    number(out, "close", c.close); number(out, "volume", c.volume); out << '}';
}
class WebSession {
public:
    WebSession() : account_(config_.owner, config_.initialCash, config_.initialInventory), reference_(config_.referencePrice),
        adapter_(book_, account_, config_), maker_(config_, account_, adapter_, reference_) {}
    bool closed() const { return closed_; }
    void handle(const std::string &line) {
        std::istringstream input(line); std::vector<std::string> words;
        for (std::string word; input >> word;) words.push_back(word);
        std::ostringstream out;
        auto time = std::chrono::system_clock::now();
        if (time < lastTime_) time = lastTime_;
        lastTime_ = time;
        try {
            // One serialized decision before a command, then all fill accounting,
            // recorder/candle updates, and measurements before the next decision.
            if (words.empty()) throw std::invalid_argument("Empty command.");
            // Control commands validate/apply reference changes before quoting;
            // stop/reset/shutdown must not create a fresh quote just to cancel it.
            if (words[0] != "MM" && words[0] != "RESET" && words[0] != "SHUTDOWN") maker_.advance(time);
            if (words[0] == "STATE" && words.size() == 1) { maker_.measure(time); writeState(out); }
            else if (words[0] == "RESET" && words.size() == 1) {
                maker_.reset(time); candles_ = CandleBuilder{}; history_.clear(); out << "{\"ok\":true}";
            } else if (words[0] == "SHUTDOWN" && words.size() == 1) {
                maker_.stop(time); closed_ = true; out << "{\"ok\":true}";
            } else if (words[0] == "ADD") add(words, time, out);
            else if (words[0] == "CANCEL") cancel(words, time, out);
            else if (words[0] == "MM") marketMaker(words, time, out);
            else throw std::invalid_argument("Unknown command.");
        } catch (const std::exception &error) {
            out.str(""); out.clear(); out << "{\"ok\":false,\"error\":"; quoted(out, error.what()); out << '}';
        }
        std::cout << out.str() << std::endl;
    }
private:
    void add(const std::vector<std::string> &words, Timestamp time, std::ostream &out) {
        OrderId id; Price price; Quantity quantity;
        if ((words.size() != 5 && words.size() != 6 && words.size() != 7) || (words[2] != "buy" && words[2] != "sell") ||
            !parseUnsigned(words[1], id) || !parseUnsigned(words[3], price) || !parseUnsigned(words[4], quantity))
            throw std::invalid_argument("Invalid order.");
        const std::string owner = words.size() >= 6 ? words[5] : "TRADER_MANUAL";
        const std::string type = words.size() == 7 ? words[6] : "limit";
        if (type != "limit" && type != "market") throw std::invalid_argument("Invalid order type.");
        if (owner != "TRADER_MANUAL" && owner != "TRADER_SIM_BUY" && owner != "TRADER_SIM_SELL")
            throw std::invalid_argument("Invalid counterparty owner.");
        auto result = adapter_.submit({{id, words[2] == "buy" ? Side::Buy : Side::Sell, price, quantity,
            type == "market" ? OrderType::Market : OrderType::Limit}, owner, config_.symbol}, time);
        if (!result.accepted()) throw std::invalid_argument(result.error);
        maker_.onExecutions(result.executions, time);
        for (const auto &execution : result.executions) { writer_.writeTrade(execution.trade); candles_.addTrade(execution.trade); history_.push_back(execution); }
        if (history_.size() > 30) history_.erase(history_.begin(), history_.end() - 30);
        out << "{\"ok\":true,\"resting\":" << (result.remaining ? "true" : "false") << ",\"type\":\"" << type << '"';
        number(out, "remaining", result.remaining); number(out, "filledQuantity", result.filledQuantity);
        number(out, "cancelledQuantity", result.cancelledQuantity); out << ",\"status\":\"";
        switch (result.status) {
            case SubmitStatus::Filled: out << "filled"; break;
            case SubmitStatus::PartiallyFilled: out << "partially_filled"; break;
            case SubmitStatus::PartiallyFilledCancelled: out << "partially_filled_cancelled"; break;
            case SubmitStatus::Cancelled: out << "cancelled"; break;
            default: out << "resting"; break;
        }
        out << "\",\"trades\":[";
        for (std::size_t i = 0; i < result.executions.size(); ++i) { if (i) out << ','; writeTrade(out, result.executions[i]); }
        out << "]}";
    }
    void cancel(const std::vector<std::string> &words, Timestamp time, std::ostream &out) {
        OrderId id;
        if ((words.size() != 2 && words.size() != 3) || !parseUnsigned(words[1], id)) throw std::invalid_argument("Invalid order ID.");
        auto result = adapter_.cancel(id, words.size() == 3 ? words[2] : "TRADER_MANUAL");
        if (result.status == CancelStatus::NotOwner) throw std::invalid_argument("This order belongs to another participant.");
        maker_.measure(time);
        out << "{\"ok\":true,\"cancelled\":" << (result.status == CancelStatus::Cancelled ? "true" : "false") << '}';
    }
    void marketMaker(const std::vector<std::string> &words, Timestamp time, std::ostream &out) {
        if (words.size() < 2) throw std::invalid_argument("Missing market-maker action.");
        if (words[1] == "STOP" && words.size() == 2) maker_.stop(time);
        else if ((words[1] == "START" || words[1] == "CONFIG") && words.size() == 10) {
            auto c = maker_.config();
            std::uint64_t *fields[] = {&c.referencePrice, &c.spreadBps, &c.quoteSize, &c.refreshIntervalMs,
                &c.skewBpsPerShare, &c.minimumInventory, &c.targetInventory, &c.maximumInventory};
            for (std::size_t i = 0; i < 8; ++i) if (!parseUnsigned(words[i + 2], *fields[i])) throw std::invalid_argument("Invalid market-maker setting.");
            maker_.configure(c, time);
            if (words[1] == "START") maker_.start(time);
        } else throw std::invalid_argument("Invalid market-maker command.");
        out << "{\"ok\":true,\"marketMaker\":"; writeMaker(out); out << '}';
    }
    void writeLevels(std::ostream &out, Side side) {
        struct Level { Quantity quantity = 0, botQuantity = 0; std::size_t count = 0; };
        std::map<Price, Level> levels;
        for (const auto &open : adapter_.openOrders()) if (open.order.side == side) {
            auto &level = levels[open.order.price]; level.quantity = checkedAdd(level.quantity, open.order.remaining); ++level.count;
            if (open.owner == config_.owner) level.botQuantity = checkedAdd(level.botQuantity, open.order.remaining);
        }
        bool first = true;
        auto write = [&](Price price, const Level &level) {
            if (!first) out << ','; first = false;
            out << "{\"price\":\"" << price << '"'; number(out, "quantity", level.quantity);
            number(out, "orderCount", level.count); number(out, "marketMakerQuantity", level.botQuantity); out << '}';
        };
        if (side == Side::Buy) { for (auto it = levels.rbegin(); it != levels.rend(); ++it) write(it->first, it->second); }
        else for (const auto &[price, level] : levels) write(price, level);
    }
    void writeMaker(std::ostream &out) {
        const auto &c = maker_.config(); const auto &m = maker_.metrics();
        auto open = adapter_.openOrders(c.owner); auto reserved = account_.commitments(open);
        out << "{\"state\":"; quoted(out, maker_.state() == MakerState::Running ? "Running" : maker_.state() == MakerState::Faulted ? "Faulted" : "Stopped");
        out << ",\"fault\":"; quoted(out, maker_.fault()); out << ",\"symbol\":"; quoted(out, c.symbol);
        number(out, "referencePrice", reference_.value()); number(out, "spreadBps", c.spreadBps); number(out, "quoteSize", c.quoteSize);
        number(out, "refreshIntervalMs", c.refreshIntervalMs); number(out, "skewBpsPerShare", c.skewBpsPerShare);
        number(out, "minimumInventory", c.minimumInventory); number(out, "targetInventory", c.targetInventory); number(out, "maximumInventory", c.maximumInventory);
        number(out, "cash", account_.cash()); number(out, "inventory", account_.inventory());
        number(out, "reservedCash", reserved.cash); number(out, "reservedShares", reserved.shares);
        number(out, "availableCash", account_.cash() - reserved.cash); number(out, "availableShares", account_.inventory() - reserved.shares);
        number(out, "portfolioValue", m.portfolioValue); number(out, "pnl", m.pnl); number(out, "excessReturn", m.excessReturn);
        number(out, "maximumDrawdown", m.maximumDrawdown); number(out, "buyFills", m.buyFills); number(out, "sellFills", m.sellFills);
        number(out, "buyVolume", m.buyVolume); number(out, "sellVolume", m.sellVolume); number(out, "rejections", m.rejections);
        number(out, "buyDisabledMs", m.buyDisabledMs); number(out, "sellDisabledMs", m.sellDisabledMs);
        number(out, "observedMinimumInventory", m.minimumInventory); number(out, "observedMaximumInventory", m.maximumInventory);
        out << ",\"buyDisabled\":" << (maker_.buyDisabled() ? "true" : "false") << ",\"sellDisabled\":" << (maker_.sellDisabled() ? "true" : "false");
        for (Side side : {Side::Buy, Side::Sell}) {
            out << (side == Side::Buy ? ",\"bid\":" : ",\"ask\":"); bool found = false;
            for (const auto &quote : open) if (quote.order.side == side) {
                out << "{\"id\":\"" << quote.order.id << '"'; number(out, "price", quote.order.price); number(out, "quantity", quote.order.remaining); out << '}'; found = true;
            }
            if (!found) out << "null";
        }
        out << '}';
    }
    void writeState(std::ostream &out) {
        out << "{\"ok\":true,\"marketOrdersSupported\":true,\"symbol\":\"LUFFY\",\"bids\":["; writeLevels(out, Side::Buy);
        out << "],\"asks\":["; writeLevels(out, Side::Sell); out << "],\"trades\":[";
        for (std::size_t i = 0; i < history_.size(); ++i) { if (i) out << ','; writeTrade(out, history_[history_.size() - 1 - i]); }
        out << "],\"candles\":["; const auto &candles = candles_.candles();
        for (std::size_t i = 0; i < candles.size(); ++i) { if (i) out << ','; writeCandle(out, candles[i]); }
        out << "],\"marketMaker\":"; writeMaker(out); out << '}';
    }
    MarketMakerConfig config_;
    OrderBook book_;
    SimulationAccount account_;
    ReferencePrice reference_;
    EngineAdapter adapter_;
    MarketMaker maker_;
    TradeCSVWriter writer_{"trades.csv"};
    CandleBuilder candles_;
    std::vector<Execution> history_;
    Timestamp lastTime_{};
    bool closed_ = false;
};
}
int main() {
    WebSession session;
    std::string line;
    while (!session.closed() && std::getline(std::cin, line)) session.handle(line);
    if (!session.closed()) session.handle("SHUTDOWN");
}
