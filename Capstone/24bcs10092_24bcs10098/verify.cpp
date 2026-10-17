#include "impl_a/orderbook_a.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(__cplusplus >= 201703L);
static_assert(std::is_abstract_v<orderbook::OrderBook>);
static_assert(!std::is_copy_constructible_v<orderbook::OrderBookA>);
static_assert(!std::is_move_constructible_v<orderbook::OrderBookA>);

namespace orderbook {

// This friend is defined only in verification code, not in the library.
struct OrderBookATestAccess {
    static bool valid(const OrderBookA& book) {
        std::size_t count = 0;
        for (const Side side : {Side::bid, Side::ask}) {
            const auto& levels = side == Side::bid ? book.bids_ : book.asks_;
            for (auto level = levels.begin(); level != levels.end(); ++level) {
                if (level->second.orders.empty() || level->second.total <= 0) return false;
                Quantity sum = 0;
                for (auto node = level->second.orders.begin();
                     node != level->second.orders.end(); ++node) {
                    const Order& order = *node;
                    if (order.side != side || order.price != level->first || order.quantity <= 0)
                        return false;
                    const auto lo = side == Side::bid ? min_bid_ticks : min_ask_ticks;
                    const auto hi = side == Side::bid ? max_bid_ticks : max_ask_ticks;
                    if (order.price < lo || order.price > hi) return false;
                    if (order.quantity > std::numeric_limits<Quantity>::max() - sum) return false;
                    sum += order.quantity;
                    const auto loc = book.live_.find(order.id);
                    if (loc == book.live_.end() || loc->second.side != side ||
                        loc->second.level != level || loc->second.order != node ||
                        book.used_ids_.count(order.id) != 1) return false;
                    ++count;
                }
                if (sum != level->second.total) return false;
            }
        }
        return count == book.live_.size() && book.used_ids_.size() >= count;
    }

    static std::vector<OrderId> fifo(const OrderBookA& book, Side side, PriceTicks price) {
        const auto& levels = side == Side::bid ? book.bids_ : book.asks_;
        const auto level = levels.find(price);
        std::vector<OrderId> ids;
        if (level != levels.end()) {
            for (const auto& order : level->second.orders) ids.push_back(order.id);
        }
        return ids;
    }

    static std::size_t used_count(const OrderBookA& book) { return book.used_ids_.size(); }
};

} // namespace orderbook

namespace {
using namespace orderbook;
constexpr Quantity max_qty = std::numeric_limits<Quantity>::max();
constexpr Depth all_depth = std::numeric_limits<Depth>::max();

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool same_levels(const DepthSnapshot& actual, const DepthSnapshot& expected) {
    if (actual.size() != expected.size()) return false;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i].price != expected[i].price ||
            actual[i].total_quantity != expected[i].total_quantity) return false;
    }
    return true;
}

void levels(const DepthSnapshot& actual, const DepthSnapshot& expected) {
    require(same_levels(actual, expected), "depth prices/quantities differ from expected");
}

void quote(const std::optional<PriceLevel>& actual, PriceTicks price, Quantity qty) {
    require(actual.has_value(), "expected BBO side is absent");
    require(actual->price == price && actual->total_quantity == qty, "incorrect BBO level");
}

void add(OrderBookA& book, OrderId id, Side side, PriceTicks price, Quantity qty,
         AddResult expected = AddResult::added) {
    const auto bids = book.get_bids(all_depth);
    const auto asks = book.get_asks(all_depth);
    const auto used = OrderBookATestAccess::used_count(book);
    require(book.add_order({id, side, price, qty}) == expected, "unexpected add result");
    require(OrderBookATestAccess::valid(book), "invariant violation after add");
    if (expected != AddResult::added) {
        levels(book.get_bids(all_depth), bids);
        levels(book.get_asks(all_depth), asks);
        require(OrderBookATestAccess::used_count(book) == used, "rejection consumed an ID");
    }
}

void cancel(OrderBookA& book, OrderId id, CancelResult expected = CancelResult::cancelled) {
    const auto bids = book.get_bids(all_depth);
    const auto asks = book.get_asks(all_depth);
    const auto used = OrderBookATestAccess::used_count(book);
    require(book.cancel_order(id) == expected, "unexpected cancellation result");
    require(OrderBookATestAccess::valid(book), "invariant violation after cancel");
    require(OrderBookATestAccess::used_count(book) == used, "cancel changed accepted-ID history");
    if (expected == CancelResult::not_found) {
        levels(book.get_bids(all_depth), bids);
        levels(book.get_asks(all_depth), asks);
    }
}

void fifo(const OrderBookA& book, Side side, PriceTicks price, std::vector<OrderId> expected) {
    require(OrderBookATestAccess::fifo(book, side, price) == expected, "FIFO sequence differs");
}

} // namespace

int main() {
    using namespace orderbook;
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"initial empty book", [] {
            OrderBookA b;
            require(!b.get_bbo().bid && !b.get_bbo().ask, "empty book has a quote");
            levels(b.get_bids(10), {}); levels(b.get_asks(10), {});
            require(OrderBookATestAccess::valid(b), "empty invariants");
        }},
        {"first bid and ID zero", [] {
            OrderBookA b; add(b, 0, Side::bid, 9000, 7);
            quote(b.get_bbo().bid, 9000, 7); require(!b.get_bbo().ask, "unexpected ask");
            levels(b.get_bids(1), {{9000, 7}}); levels(b.get_asks(1), {});
        }},
        {"first ask with independent empty bid", [] {
            OrderBookA b; add(b, 1, Side::ask, 11000, 9);
            quote(b.get_bbo().ask, 11000, 9); require(!b.get_bbo().bid, "unexpected bid");
            levels(b.get_asks(1), {{11000, 9}}); levels(b.get_bids(1), {});
        }},
        {"best prices change on add and cancel", [] {
            OrderBookA b;
            add(b, 1, Side::bid, 8000, 3); add(b, 2, Side::bid, 9900, 4);
            add(b, 3, Side::ask, 14000, 5); add(b, 4, Side::ask, 10100, 6);
            quote(b.get_bbo().bid, 9900, 4); quote(b.get_bbo().ask, 10100, 6);
            cancel(b, 2); cancel(b, 4);
            quote(b.get_bbo().bid, 8000, 3); quote(b.get_bbo().ask, 14000, 5);
        }},
        {"same-price aggregates on both sides", [] {
            OrderBookA b;
            add(b, 1, Side::bid, 9000, 7); add(b, 2, Side::bid, 9000, 11);
            add(b, 3, Side::ask, 11000, 13); add(b, 4, Side::ask, 11000, 17);
            levels(b.get_bids(10), {{9000, 18}}); levels(b.get_asks(10), {{11000, 30}});
            quote(b.get_bbo().bid, 9000, 18); quote(b.get_bbo().ask, 11000, 30);
        }},
        {"bid FIFO first middle last cancellation", [] {
            OrderBookA b;
            add(b, 90, Side::bid, 9000, 2); add(b, 10, Side::bid, 9000, 3);
            add(b, 70, Side::bid, 9000, 5); add(b, 20, Side::bid, 9000, 7);
            add(b, 50, Side::bid, 9000, 11);
            fifo(b, Side::bid, 9000, {90, 10, 70, 20, 50});
            cancel(b, 90); fifo(b, Side::bid, 9000, {10, 70, 20, 50});
            cancel(b, 70); fifo(b, Side::bid, 9000, {10, 20, 50});
            cancel(b, 50); fifo(b, Side::bid, 9000, {10, 20});
            levels(b.get_bids(10), {{9000, 10}});
            add(b, 5, Side::bid, 9000, 13); fifo(b, Side::bid, 9000, {10, 20, 5});
        }},
        {"ask FIFO first middle last cancellation", [] {
            OrderBookA b;
            add(b, 8, Side::ask, 11000, 2); add(b, 2, Side::ask, 11000, 3);
            add(b, 7, Side::ask, 11000, 5); add(b, 1, Side::ask, 11000, 7);
            add(b, 6, Side::ask, 11000, 11);
            fifo(b, Side::ask, 11000, {8, 2, 7, 1, 6});
            cancel(b, 8); cancel(b, 7); cancel(b, 6);
            fifo(b, Side::ask, 11000, {2, 1}); levels(b.get_asks(10), {{11000, 10}});
        }},
        {"final orders remove levels and empty sides", [] {
            OrderBookA b;
            add(b, 1, Side::bid, 9999, 2); add(b, 2, Side::bid, 9999, 3);
            add(b, 3, Side::ask, 10001, 4);
            cancel(b, 1); levels(b.get_bids(10), {{9999, 3}});
            cancel(b, 2); levels(b.get_bids(10), {});
            require(!b.get_bbo().bid, "bid level survived final cancellation");
            quote(b.get_bbo().ask, 10001, 4); cancel(b, 3);
            require(!b.get_bbo().bid && !b.get_bbo().ask, "book not empty");
            add(b, 4, Side::bid, 9999, 8); levels(b.get_bids(10), {{9999, 8}});
        }},
        {"unknown and repeated cancellations", [] {
            OrderBookA b; cancel(b, 77, CancelResult::not_found);
            add(b, 1, Side::bid, 9000, 8); cancel(b, 99, CancelResult::not_found);
            cancel(b, 1); cancel(b, 1, CancelResult::not_found);
            add(b, 77, Side::ask, 11000, 3); quote(b.get_bbo().ask, 11000, 3);
        }},
        {"live and cancelled IDs cannot be reused across sides", [] {
            OrderBookA b; add(b, 0, Side::bid, 9000, 4);
            add(b, 0, Side::ask, 11000, 6, AddResult::duplicate_id);
            cancel(b, 0);
            add(b, 0, Side::bid, 9000, 4, AddResult::duplicate_id);
            add(b, 0, Side::ask, 11000, 6, AddResult::duplicate_id);
            require(!b.get_bbo().bid && !b.get_bbo().ask, "reused ID entered book");
        }},
        {"rejected adds leave IDs available", [] {
            OrderBookA b;
            add(b, 1, Side::bid, 10000, 1, AddResult::invalid_price);
            add(b, 1, Side::bid, 9999, 1);
            add(b, 2, Side::ask, 10001, 0, AddResult::invalid_quantity);
            add(b, 2, Side::ask, 10001, 2);
            add(b, 3, static_cast<Side>(5), 9000, 1, AddResult::invalid_side);
            add(b, 3, Side::bid, 9000, 3);
        }},
        {"all four inclusive price boundaries", [] {
            OrderBookA b;
            add(b, 1, Side::bid, 5000, 1); add(b, 2, Side::bid, 9999, 2);
            add(b, 3, Side::ask, 10001, 3); add(b, 4, Side::ask, 15000, 4);
            levels(b.get_bids(10), {{9999, 2}, {5000, 1}});
            levels(b.get_asks(10), {{10001, 3}, {15000, 4}});
        }},
        {"invalid bid and ask prices", [] {
            OrderBookA b; OrderId id = 1;
            for (PriceTicks price : {4999, 10000, 10001, -1,
                                    std::numeric_limits<PriceTicks>::max()})
                add(b, id++, Side::bid, price, 1, AddResult::invalid_price);
            for (PriceTicks price : {10000, 15001, 9999, -1,
                                    std::numeric_limits<PriceTicks>::min()})
                add(b, id++, Side::ask, price, 1, AddResult::invalid_price);
            require(OrderBookATestAccess::used_count(b) == 0, "invalid prices consumed IDs");
        }},
        {"zero negative and minimum quantities", [] {
            OrderBookA b; OrderId id = 1;
            for (Side side : {Side::bid, Side::ask}) {
                for (Quantity qty : {Quantity{0}, Quantity{-1}, std::numeric_limits<Quantity>::min()})
                    add(b, id++, side, side == Side::bid ? 9000 : 11000, qty,
                        AddResult::invalid_quantity);
            }
            require(!b.get_bbo().bid && !b.get_bbo().ask, "invalid quantity stored");
        }},
        {"invalid sides and validation precedence", [] {
            OrderBookA b; add(b, 1, Side::bid, 9000, 5);
            add(b, 1, static_cast<Side>(-1), -1, -1, AddResult::invalid_side);
            add(b, 1, Side::bid, 10000, 0, AddResult::invalid_price);
            add(b, 1, Side::bid, 9000, 0, AddResult::invalid_quantity);
            add(b, 1, Side::bid, 9000, 1, AddResult::duplicate_id);
        }},
        {"bid quantity overflow rollback and ID retry", [] {
            OrderBookA b; add(b, 1, Side::bid, 9000, max_qty - 2);
            add(b, 2, Side::bid, 9000, 2); quote(b.get_bbo().bid, 9000, max_qty);
            add(b, 3, Side::bid, 9000, 1, AddResult::quantity_overflow);
            fifo(b, Side::bid, 9000, {1, 2});
            add(b, 4, Side::bid, 8999, max_qty);
            cancel(b, 2); add(b, 3, Side::bid, 9000, 1);
            levels(b.get_bids(10), {{9000, max_qty - 1}, {8999, max_qty}});
        }},
        {"ask maximum quantity overflow and recovery", [] {
            OrderBookA b; add(b, 1, Side::ask, 10001, max_qty);
            add(b, 2, Side::ask, 10001, max_qty, AddResult::quantity_overflow);
            quote(b.get_bbo().ask, 10001, max_qty); cancel(b, 1);
            add(b, 2, Side::ask, 10001, max_qty);
            levels(b.get_asks(10), {{10001, max_qty}});
        }},
        {"negative zero normal and oversized depth", [] {
            OrderBookA b;
            add(b, 1, Side::bid, 7000, 1); add(b, 2, Side::bid, 9900, 2);
            add(b, 3, Side::bid, 8000, 3); add(b, 4, Side::ask, 14000, 4);
            add(b, 5, Side::ask, 10100, 5); add(b, 6, Side::ask, 12000, 6);
            for (Depth depth : {std::numeric_limits<Depth>::min(), Depth{-1}, Depth{0}}) {
                levels(b.get_bids(depth), {}); levels(b.get_asks(depth), {});
            }
            levels(b.get_bids(2), {{9900, 2}, {8000, 3}});
            levels(b.get_asks(2), {{10100, 5}, {12000, 6}});
            levels(b.get_bids(all_depth), {{9900, 2}, {8000, 3}, {7000, 1}});
            levels(b.get_asks(all_depth), {{10100, 5}, {12000, 6}, {14000, 4}});
        }},
        {"owned snapshots remain stable after mutations", [] {
            OrderBookA b; add(b, 1, Side::bid, 9000, 5); add(b, 2, Side::ask, 11000, 7);
            auto bids = b.get_bids(10); auto asks = b.get_asks(10); const auto bbo = b.get_bbo();
            cancel(b, 1); cancel(b, 2); add(b, 3, Side::bid, 9999, 11);
            levels(bids, {{9000, 5}}); levels(asks, {{11000, 7}});
            quote(bbo.bid, 9000, 5); quote(bbo.ask, 11000, 7);
            bids.front().total_quantity = 123; asks.clear();
            levels(b.get_bids(10), {{9999, 11}}); levels(b.get_asks(10), {});
        }},
        {"uint64 maximum ID and interface dispatch", [] {
            OrderBookA b; OrderBook& api = b;
            const auto id = std::numeric_limits<OrderId>::max();
            require(api.add_order({id, Side::ask, 15000, 8}) == AddResult::added, "dispatch add");
            quote(api.get_bbo().ask, 15000, 8); levels(api.get_asks(1), {{15000, 8}});
            levels(api.get_bids(1), {});
            require(api.cancel_order(id) == CancelResult::cancelled, "dispatch cancel");
            require(api.add_order({id, Side::bid, 5000, 1}) == AddResult::duplicate_id, "ID history");
            require(OrderBookATestAccess::valid(b), "dispatch invariants");
        }}
    };
    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.second(); ++passed;
            std::cout << "PASS: " << test.first << '\n';
        } catch (const std::exception& e) {
            std::cout << "FAIL: " << test.first << ": " << e.what() << '\n';
        } catch (...) {
            std::cout << "FAIL: " << test.first << ": unknown exception\n";
        }
    }
    std::cout << "Implementation A: " << passed << " PASS, " << tests.size() - passed
              << " FAIL, " << tests.size() << " tests. B and C not implemented/tested.\n";
    return passed == tests.size() ? 0 : 1;
}
