// Reuse the class in the single-file learning project without its test main.
#define ORDERBOOK_LIBRARY_ONLY
#include "../FINALorderbook.cpp"

#include <charconv>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace
{
bool parseUnsigned(const std::string &text, std::uint64_t &value)
{
    const char *end = text.data() + text.size();
    auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

void writeTrade(std::ostream &out, const Trade &trade)
{
    out << "{\"makerId\":\"" << trade.makerId
        << "\",\"takerId\":\"" << trade.takerId
        << "\",\"price\":\"" << trade.price
        << "\",\"quantity\":\"" << trade.quantity << "\"}";
}

void writeError(const char *message)
{
    std::cout << "{\"ok\":false,\"error\":\"" << message << "\"}" << std::endl;
}

class WebSession
{
public:
    void handle(const std::string &line)
    {
        std::istringstream input(line);
        std::vector<std::string> words;
        for (std::string word; input >> word;)
        {
            words.push_back(word);
        }
        if (words.empty())
        {
            writeError("Empty command.");
            return;
        }

        if (words[0] == "STATE" && words.size() == 1)
        {
            writeState();
        }
        else if (words[0] == "RESET" && words.size() == 1)
        {
            book_ = OrderBook{};
            bidPrices_.clear();
            askPrices_.clear();
            history_.clear();
            std::cout << "{\"ok\":true}" << std::endl;
        }
        else if (words[0] == "ADD")
        {
            add(words);
        }
        else if (words[0] == "CANCEL")
        {
            cancel(words);
        }
        else
        {
            writeError("Unknown command.");
        }
    }

private:
    void add(const std::vector<std::string> &words)
    {
        OrderId id;
        Price price;
        Quantity quantity;
        if (words.size() != 5 || (words[2] != "buy" && words[2] != "sell") ||
            !parseUnsigned(words[1], id) || !parseUnsigned(words[3], price) ||
            !parseUnsigned(words[4], quantity))
        {
            writeError("Invalid order.");
            return;
        }

        Side side = words[2] == "buy" ? Side::Buy : Side::Sell;
        try
        {
            std::vector<Trade> trades = book_.addOrder({id, side, price, quantity});
            if (side == Side::Buy)
            {
                bidPrices_.insert(price);
            }
            else
            {
                askPrices_.insert(price);
            }
            history_.insert(history_.end(), trades.begin(), trades.end());
            if (history_.size() > 30)
            {
                history_.erase(history_.begin(), history_.begin() + (history_.size() - 30));
            }

            std::cout << "{\"ok\":true,\"resting\":"
                      << (book_.contains(id) ? "true" : "false") << ",\"trades\":[";
            for (std::size_t i = 0; i < trades.size(); ++i)
            {
                if (i > 0)
                {
                    std::cout << ',';
                }
                writeTrade(std::cout, trades[i]);
            }
            std::cout << "]}" << std::endl;
        }
        catch (const std::invalid_argument &error)
        {
            // The class currently uses fixed messages with no JSON special characters.
            std::cout << "{\"ok\":false,\"error\":\"" << error.what() << "\"}" << std::endl;
        }
    }

    void cancel(const std::vector<std::string> &words)
    {
        OrderId id;
        if (words.size() != 2 || !parseUnsigned(words[1], id))
        {
            writeError("Invalid order ID.");
            return;
        }
        std::cout << "{\"ok\":true,\"cancelled\":"
                  << (book_.cancelOrder(id) ? "true" : "false") << "}" << std::endl;
    }

    template <typename Prices>
    void writeLevels(const Prices &prices, Side side)
    {
        bool first = true;
        for (Price price : prices)
        {
            std::size_t count = book_.orderCountAt(side, price);
            if (count == 0)
            {
                continue;
            }
            if (!first)
            {
                std::cout << ',';
            }
            first = false;
            std::cout << "{\"price\":\"" << price
                      << "\",\"quantity\":\"" << book_.quantityAt(side, price)
                      << "\",\"orderCount\":\"" << count << "\"}";
        }
    }

    void writeState()
    {
        std::cout << "{\"ok\":true,\"bids\":[";
        writeLevels(bidPrices_, Side::Buy);
        std::cout << "],\"asks\":[";
        writeLevels(askPrices_, Side::Sell);
        std::cout << "],\"trades\":[";
        for (std::size_t i = 0; i < history_.size(); ++i)
        {
            if (i > 0)
            {
                std::cout << ',';
            }
            writeTrade(std::cout, history_[history_.size() - 1 - i]);
        }
        std::cout << "]}" << std::endl;
    }

    OrderBook book_;
    std::set<Price, std::greater<Price>> bidPrices_;
    std::set<Price> askPrices_;
    std::vector<Trade> history_;
};
} // namespace

int main()
{
    WebSession session;
    std::string line;
    while (std::getline(std::cin, line))
    {
        session.handle(line);
    }
}
