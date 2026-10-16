#include "market_engine/engine_adapter.hpp"
#include "market_engine/checked_math.hpp"

namespace market_engine {
EngineAdapter::EngineAdapter(OrderBook &book, SimulationAccount &account, const MarketMakerConfig &config)
    : book_(book), account_(account), config_(config) { config.validate(); }
std::vector<OpenOrder> EngineAdapter::openOrders(const std::string &owner) const {
    std::vector<OpenOrder> result;
    for (const auto &[id, stored] : orders_) {
        if (!owner.empty() && stored.owner != owner) continue;
        auto actual = book_.findOrder(id);
        if (actual) { auto copy = stored; copy.order = *actual; result.push_back(copy); }
    }
    return result;
}
BookSnapshot EngineAdapter::snapshot(const std::string &excludeOwner) const {
    BookSnapshot result;
    for (const auto &open : openOrders()) {
        if (open.owner == excludeOwner) continue;
        auto &best = open.order.side == Side::Buy ? result.bestBid : result.bestAsk;
        if (!best || (open.order.side == Side::Buy ? open.order.price > *best : open.order.price < *best)) best = open.order.price;
    }
    return result;
}
SubmitResult EngineAdapter::submit(const OrderRequest &request, Timestamp time) {
    SubmitResult result;
    try {
        const auto &order = request.order;
        const bool market = order.type == OrderType::Market;
        if ((order.type != OrderType::Market && order.type != OrderType::Limit) ||
            (order.side != Side::Buy && order.side != Side::Sell) ||
            !order.id || (!market && !order.price) || !order.remaining || request.owner.empty() || request.symbol != config_.symbol)
            throw std::invalid_argument("Invalid order or symbol.");
        if (market && order.price) throw std::invalid_argument("Market orders do not take a limit price.");
        if (market && (request.postOnly || request.owner == config_.owner))
            throw std::invalid_argument("Market orders cannot be post-only; the maker only submits passive limit orders.");
        if (usedIds_.contains(order.id)) throw std::invalid_argument("Order with this ID already exists.");
        const auto snapshotAll = snapshot();
        const bool crosses = order.side == Side::Buy ? snapshotAll.bestAsk && (market || order.price >= *snapshotAll.bestAsk)
                                                    : snapshotAll.bestBid && (market || order.price <= *snapshotAll.bestBid);
        if ((request.postOnly || request.owner == config_.owner) && crosses)
            throw std::invalid_argument("Post-only order would immediately execute.");
        if (!market) {
            for (const auto &open : openOrders(request.owner)) {
                if (open.order.side != order.side && (order.side == Side::Buy ? order.price >= open.order.price : order.price <= open.order.price))
                    throw std::invalid_argument("Order would trade with its own owner.");
            }
        } else {
            // Preflight the levels/FIFO orders this market request would reach.
            // A worse own quote beyond its requested quantity does not block a fill.
            auto opposing = openOrders();
            std::erase_if(opposing, [&](const OpenOrder &open) { return open.order.side == order.side; });
            std::sort(opposing.begin(), opposing.end(), [&](const OpenOrder &a, const OpenOrder &b) {
                if (a.order.price == b.order.price) return a.arrivalSequence < b.arrivalSequence;
                return order.side == Side::Buy ? a.order.price < b.order.price : a.order.price > b.order.price;
            });
            auto demand = order.remaining;
            for (const auto &open : opposing) {
                if (!demand) break;
                if (open.owner == request.owner) throw std::invalid_argument("Order would trade with its own owner.");
                demand -= std::min(demand, open.order.remaining);
            }
        }
        if (request.owner == config_.owner) {
            auto existing = openOrders(config_.owner);
            for (const auto &open : existing) if (open.order.side == order.side)
                throw std::invalid_argument("Only one bot quote per side is allowed.");
            const auto permitted = order.side == Side::Buy
                ? account_.permittedBuy(order.price, order.remaining, config_, existing)
                : account_.permittedSell(order.remaining, config_, existing);
            if (permitted < order.remaining) throw std::invalid_argument("Insufficient resources or inventory capacity.");
            // Sells execute at their passive limit; reject potential cash-credit overflow now.
            if (order.side == Side::Sell) signedTicks(checkedAdd(account_.cash(), checkedMultiply(order.price, order.remaining)));
        }
        if (nextArrival_ == UINT64_MAX || nextExecution_ > UINT64_MAX - orders_.size() - 1)
            throw std::overflow_error("Simulation sequence exhausted.");
        orders_.emplace(order.id, OpenOrder{order, request.owner, request.symbol, nextArrival_, request.postOnly});
        std::vector<Trade> trades;
        try { trades = book_.addOrder(order, time); }
        catch (...) { orders_.erase(order.id); throw; }
        usedIds_.insert(order.id); ++nextArrival_;
        // Resolve ownership before pruning fully filled orders from the lookup.
        auto unfilled = order.remaining;
        for (const auto &trade : trades) {
            unfilled -= trade.quantity;
            const auto &maker = orders_.at(trade.makerId);
            const bool buy = order.side == Side::Buy;
            result.executions.push_back({nextExecution_, nextExecution_, trade, request.symbol,
                buy ? trade.takerId : trade.makerId, buy ? trade.makerId : trade.takerId,
                buy ? request.owner : maker.owner, buy ? maker.owner : request.owner});
            ++nextExecution_;
        }
        auto remaining = book_.findOrder(order.id);
        result.remaining = remaining ? remaining->remaining : 0;
        result.filledQuantity = order.remaining - unfilled;
        result.cancelledQuantity = market ? unfilled : 0;
        result.status = market && unfilled ? (trades.empty() ? SubmitStatus::Cancelled : SubmitStatus::PartiallyFilledCancelled)
            : !remaining ? SubmitStatus::Filled : trades.empty() ? SubmitStatus::Resting : SubmitStatus::PartiallyFilled;
        for (auto it = orders_.begin(); it != orders_.end();) {
            if (!book_.contains(it->first)) it = orders_.erase(it); else ++it;
        }
    } catch (const std::exception &error) { result.error = error.what(); }
    return result;
}
CancelResult EngineAdapter::cancel(OrderId id, const std::string &requester) {
    auto found = orders_.find(id);
    auto actual = book_.findOrder(id);
    if (found == orders_.end() || !actual) return {CancelStatus::NotOpen};
    if (found->second.owner != requester) return {CancelStatus::NotOwner};
    if (!book_.cancelOrder(id)) return {CancelStatus::NotOpen};
    orders_.erase(found);
    return {CancelStatus::Cancelled, actual->remaining};
}
OrderId EngineAdapter::allocateBotId() {
    while (usedIds_.contains(nextBotId_)) nextBotId_ = checkedAdd(nextBotId_, 1);
    auto id = nextBotId_; nextBotId_ = checkedAdd(nextBotId_, 1); return id;
}
void EngineAdapter::reset() {
    book_ = OrderBook{}; orders_.clear(); usedIds_.clear(); nextExecution_ = 1; nextArrival_ = 1; nextBotId_ = 1ULL << 62;
}
}
