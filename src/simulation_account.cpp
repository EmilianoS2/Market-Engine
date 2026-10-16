#include "market_engine/simulation_account.hpp"
#include "market_engine/checked_math.hpp"

namespace market_engine {
SimulationAccount::SimulationAccount(std::string owner, std::uint64_t cash, Quantity inventory)
    : owner_(std::move(owner)), cash_(cash), inventory_(inventory) {
    signedTicks(cash); signedTicks(inventory);
}
void SimulationAccount::reset(std::uint64_t cash, Quantity inventory) {
    signedTicks(cash); signedTicks(inventory);
    cash_ = cash; inventory_ = inventory; applied_.clear();
}
bool SimulationAccount::apply(const Execution &execution) {
    const bool buy = execution.buyer == owner_, sell = execution.seller == owner_;
    if (!buy && !sell) return false;
    if (buy && sell) throw std::invalid_argument("Self-trade notification rejected.");
    if (applied_.contains(execution.id)) return false;
    if (!execution.trade.quantity || !execution.trade.price) throw std::invalid_argument("Invalid execution.");
    const auto cost = checkedMultiply(execution.trade.price, execution.trade.quantity);
    auto cash = cash_;
    auto inventory = inventory_;
    if (buy) {
        if (cost > cash) throw std::invalid_argument("Insufficient cash for execution.");
        cash -= cost; inventory = checkedAdd(inventory, execution.trade.quantity);
    } else {
        if (execution.trade.quantity > inventory) throw std::invalid_argument("Insufficient shares for execution.");
        cash = checkedAdd(cash, cost); inventory -= execution.trade.quantity;
    }
    signedTicks(cash); signedTicks(inventory);
    applied_.insert(execution.id);
    cash_ = cash; inventory_ = inventory;
    return true;
}
Commitments SimulationAccount::commitments(const std::vector<OpenOrder> &orders) const {
    Commitments result;
    for (const auto &open : orders) {
        if (open.owner != owner_) continue;
        if (open.order.side == Side::Buy) {
            result.cash = checkedAdd(result.cash, checkedMultiply(open.order.price, open.order.remaining));
            result.buys = checkedAdd(result.buys, open.order.remaining);
        } else {
            result.shares = checkedAdd(result.shares, open.order.remaining);
            result.sells = checkedAdd(result.sells, open.order.remaining);
        }
    }
    if (result.cash > cash_ || result.shares > inventory_)
        throw std::logic_error("Outstanding orders exceed account resources.");
    return result;
}
Quantity SimulationAccount::permittedBuy(Price price, Quantity size, const MarketMakerConfig &config,
                                         const std::vector<OpenOrder> &orders) const {
    if (!price) return 0;
    auto reserved = commitments(orders);
    if (inventory_ >= config.maximumInventory || reserved.buys >= config.maximumInventory - inventory_) return 0;
    return std::min({size, config.maximumInventory - inventory_ - reserved.buys, (cash_ - reserved.cash) / price});
}
Quantity SimulationAccount::permittedSell(Quantity size, const MarketMakerConfig &config,
                                          const std::vector<OpenOrder> &orders) const {
    auto reserved = commitments(orders);
    if (inventory_ <= config.minimumInventory || reserved.sells >= inventory_ - config.minimumInventory) return 0;
    return std::min({size, inventory_ - config.minimumInventory - reserved.sells, inventory_ - reserved.shares});
}
std::uint64_t SimulationAccount::portfolioValue(Price reference) const {
    auto result = checkedAdd(cash_, checkedMultiply(inventory_, reference));
    signedTicks(result);
    return result;
}
}
