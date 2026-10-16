#include "market_engine/market_maker.hpp"
#include "market_engine/checked_math.hpp"

namespace market_engine {
std::uint64_t milliseconds(Timestamp time) {
    auto value = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    if (value < 0) throw std::invalid_argument("Simulation time must be nonnegative.");
    return static_cast<std::uint64_t>(value);
}
Timestamp simulatedTime(std::uint64_t time) {
    signedTicks(time);
    // system_clock often stores nanoseconds; validate conversion too.
    using Duration = Timestamp::duration;
    const auto maximum = std::chrono::duration_cast<std::chrono::milliseconds>(Duration::max()).count();
    if (time > static_cast<std::uint64_t>(maximum)) throw std::overflow_error("Timestamp overflow.");
    return Timestamp{std::chrono::duration_cast<Duration>(std::chrono::milliseconds(time))};
}
MarketMaker::MarketMaker(MarketMakerConfig config, SimulationAccount &account, EngineAdapter &adapter, ReferencePrice &reference)
    : config_(std::move(config)), account_(account), adapter_(adapter), reference_(reference),
      initialValue_(account.portfolioValue(reference.value())), initialCash_(account.cash()), peakValue_(initialValue_),
      initialInventory_(account.inventory()), initialReference_(reference.value()) {
    config_.validate();
    if (account.owner() != config_.owner || account.inventory() < config_.minimumInventory || account.inventory() > config_.maximumInventory)
        throw std::invalid_argument("Account does not match market-maker configuration.");
    metrics_.minimumInventory = metrics_.maximumInventory = account.inventory();
    metrics_.portfolioValue = initialValue_;
}
void MarketMaker::cancelQuotes() {
    for (const auto &open : adapter_.openOrders(config_.owner)) adapter_.cancel(open.order.id, config_.owner);
    if (!adapter_.openOrders(config_.owner).empty()) throw std::logic_error("Bot shutdown left open orders.");
}
void MarketMaker::fail(const std::string &reason, Timestamp time) {
    state_ = MakerState::Faulted; fault_ = reason;
    cancelQuotes(); buyDisabled_ = sellDisabled_ = false;
    // Fault reporting must remain available even if valuation overflow caused the fault.
    try { measure(time); } catch (const std::exception &) {}
}
void MarketMaker::start(Timestamp time) {
    if (state_ == MakerState::Running) return;
    if (state_ == MakerState::Faulted) throw std::invalid_argument("Clear the simulation before restarting a faulted bot.");
    config_.validate(); account_.portfolioValue(reference_.value());
    simulatedTime(checkedAdd(milliseconds(time), config_.refreshIntervalMs));
    state_ = MakerState::Running; refresh(time);
}
void MarketMaker::stop(Timestamp time) {
    cancelQuotes();
    if (state_ != MakerState::Faulted) state_ = MakerState::Stopped;
    if (state_ == MakerState::Faulted) { try { measure(time); } catch (const std::exception &) {} }
    else measure(time);
}
void MarketMaker::configure(const MarketMakerConfig &config, Timestamp time) {
    config.validate();
    simulatedTime(checkedAdd(milliseconds(time), config.refreshIntervalMs));
    if (config.owner != config_.owner || config.symbol != config_.symbol ||
        config.initialCash != config_.initialCash || config.initialInventory != config_.initialInventory)
        throw std::invalid_argument("Account identity and endowments cannot change during a session.");
    if (account_.inventory() < config.minimumInventory || account_.inventory() > config.maximumInventory)
        throw std::invalid_argument("Current inventory is outside the proposed limits.");
    account_.portfolioValue(config.referencePrice);
    calculateQuotes(config, config.referencePrice, account_.inventory(), adapter_.snapshot(config.owner));
    measure(time);
    config_ = config; adapter_.configure(config); reference_.set(config.referencePrice);
    if (state_ == MakerState::Running) refresh(time); else measure(time);
}
void MarketMaker::advance(Timestamp time) {
    if (state_ == MakerState::Running && milliseconds(time) >= nextDecisionMs_) refresh(time);
}
void MarketMaker::refresh(Timestamp time) {
    if (state_ != MakerState::Running) return;
    try {
        const auto nextDecision = checkedAdd(milliseconds(time), config_.refreshIntervalMs);
        simulatedTime(nextDecision);
        auto prices = calculateQuotes(config_, reference_.value(), account_.inventory(), adapter_.snapshot(config_.owner));
        auto open = adapter_.openOrders(config_.owner);
        for (const auto &quote : open) {
            auto others = open;
            std::erase_if(others, [&](const OpenOrder &item) { return item.order.id == quote.order.id; });
            const auto price = quote.order.side == Side::Buy ? prices.bid : prices.ask;
            const auto allowed = quote.order.side == Side::Buy
                ? price ? account_.permittedBuy(*price, config_.quoteSize, config_, others) : 0
                : price ? account_.permittedSell(config_.quoteSize, config_, others) : 0;
            // Keep an eligible partial remainder; do not replenish beside it.
            if (!price || quote.order.price != *price || quote.order.remaining > allowed)
                adapter_.cancel(quote.order.id, config_.owner);
        }
        prices = calculateQuotes(config_, reference_.value(), account_.inventory(), adapter_.snapshot(config_.owner));
        for (Side side : {Side::Buy, Side::Sell}) {
            auto current = adapter_.openOrders(config_.owner);
            if (std::any_of(current.begin(), current.end(), [&](const OpenOrder &quote) { return quote.order.side == side; })) continue;
            const auto price = side == Side::Buy ? prices.bid : prices.ask;
            if (!price) continue;
            const auto quantity = side == Side::Buy ? account_.permittedBuy(*price, config_.quoteSize, config_, current)
                                                    : account_.permittedSell(config_.quoteSize, config_, current);
            if (!quantity) continue;
            auto result = adapter_.submit({{adapter_.allocateBotId(), side, *price, quantity}, config_.owner, config_.symbol, true}, time);
            if (!result.accepted()) { ++metrics_.rejections; throw std::runtime_error(result.error); }
            if (!result.executions.empty()) throw std::logic_error("Passive bot quote unexpectedly executed.");
        }
        nextDecisionMs_ = nextDecision;
        measure(time);
    } catch (const std::exception &error) { fail(error.what(), time); }
}
void MarketMaker::onExecutions(const std::vector<Execution> &executions, Timestamp time) {
    try {
        // Validate a complete command's notifications before committing account changes.
        auto candidate = account_;
        auto metrics = metrics_;
        for (const auto &execution : executions) {
            if (!candidate.apply(execution)) continue;
            const bool buy = execution.buyer == config_.owner;
            auto &fills = buy ? metrics.buyFills : metrics.sellFills;
            auto &volume = buy ? metrics.buyVolume : metrics.sellVolume;
            fills = checkedAdd(fills, 1); volume = checkedAdd(volume, execution.trade.quantity);
            metrics.minimumInventory = std::min(metrics.minimumInventory, candidate.inventory());
            metrics.maximumInventory = std::max(metrics.maximumInventory, candidate.inventory());
        }
        candidate.portfolioValue(reference_.value());
        account_ = std::move(candidate); metrics_ = metrics;
        measure(time);
    } catch (const std::exception &error) { fail(error.what(), time); }
}
void MarketMaker::measure(Timestamp time) {
    const auto now = milliseconds(time);
    if (measured_ && now < lastMeasurementMs_) throw std::invalid_argument("Simulation clock moved backwards.");
    const auto elapsed = measured_ ? now - lastMeasurementMs_ : 0;
    if (buyDisabled_) metrics_.buyDisabledMs = checkedAdd(metrics_.buyDisabledMs, elapsed);
    if (sellDisabled_) metrics_.sellDisabledMs = checkedAdd(metrics_.sellDisabledMs, elapsed);
    lastMeasurementMs_ = now; measured_ = true;
    metrics_.portfolioValue = account_.portfolioValue(reference_.value());
    peakValue_ = std::max(peakValue_, metrics_.portfolioValue);
    metrics_.maximumDrawdown = std::max(metrics_.maximumDrawdown, peakValue_ - metrics_.portfolioValue);
    metrics_.pnl = difference(metrics_.portfolioValue, initialValue_);
    const auto inventoryDifference = difference(account_.inventory(), initialInventory_);
    const auto inventoryMark = checkedMultiply(static_cast<std::uint64_t>(inventoryDifference < 0 ? -inventoryDifference : inventoryDifference), reference_.value());
    metrics_.excessReturn = checkedSignedAdd(difference(account_.cash(), initialCash_), inventoryDifference < 0 ? -signedTicks(inventoryMark) : signedTicks(inventoryMark));
    buyDisabled_ = sellDisabled_ = false;
    if (state_ == MakerState::Running) {
        const auto prices = calculateQuotes(config_, reference_.value(), account_.inventory(), adapter_.snapshot(config_.owner));
        auto open = adapter_.openOrders(config_.owner);
        auto buyOthers = open, sellOthers = open;
        std::erase_if(buyOthers, [](const OpenOrder &quote) { return quote.order.side == Side::Buy; });
        std::erase_if(sellOthers, [](const OpenOrder &quote) { return quote.order.side == Side::Sell; });
        buyDisabled_ = !prices.bid || !account_.permittedBuy(*prices.bid, config_.quoteSize, config_, buyOthers);
        sellDisabled_ = !prices.ask || !account_.permittedSell(config_.quoteSize, config_, sellOthers);
    }
}
void MarketMaker::reset(Timestamp time) {
    cancelQuotes(); reference_.set(config_.referencePrice);
    adapter_.reset(); account_.reset(config_.initialCash, config_.initialInventory);
    initialCash_ = account_.cash(); initialInventory_ = account_.inventory(); initialReference_ = reference_.value();
    initialValue_ = peakValue_ = account_.portfolioValue(reference_.value());
    metrics_ = {}; metrics_.minimumInventory = metrics_.maximumInventory = account_.inventory();
    state_ = MakerState::Stopped; fault_.clear(); measured_ = false; buyDisabled_ = sellDisabled_ = false;
    measure(time);
}
}
