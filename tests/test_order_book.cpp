/**
 * @file test_order_book.cpp
 * @brief Unit tests for Order Book Engine
 */

#include "../include/order_book.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace itch;

// =============================================================================
// Test Utilities
// =============================================================================

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "  " << #name << "... "; \
    test_##name(); \
    std::cout << "PASSED\n"; \
} while(0)

// =============================================================================
// Object Pool Tests
// =============================================================================

TEST(object_pool_acquire_release) {
    ObjectPool<Order, 100> pool;
    
    assert(pool.capacity() == 100);
    assert(pool.available() == 100);
    
    // Acquire some objects
    std::vector<Order*> orders;
    for (int i = 0; i < 50; ++i) {
        orders.push_back(pool.acquire());
    }
    
    assert(pool.available() == 50);
    
    // Release them back
    for (auto* order : orders) {
        pool.release(order);
    }
    
    assert(pool.available() == 100);
}

TEST(object_pool_growth) {
    ObjectPool<Order, 10> pool;
    
    assert(pool.capacity() == 10);
    
    // Acquire more than initial capacity
    std::vector<Order*> orders;
    for (int i = 0; i < 25; ++i) {
        orders.push_back(pool.acquire());
    }
    
    // Pool should have grown
    assert(pool.capacity() >= 25);
    
    // Release all
    for (auto* order : orders) {
        pool.release(order);
    }
}

// =============================================================================
// Price Level Tests
// =============================================================================

TEST(price_level_add_remove) {
    ObjectPool<Order> pool;
    PriceLevel level(1500000);
    
    assert(level.price() == 1500000);
    assert(level.empty());
    assert(level.total_quantity() == 0);
    assert(level.order_count() == 0);
    
    // Add orders
    Order* order1 = pool.acquire();
    order1->order_id = 1;
    order1->quantity = 100;
    level.add_order(order1);
    
    Order* order2 = pool.acquire();
    order2->order_id = 2;
    order2->quantity = 200;
    level.add_order(order2);
    
    assert(!level.empty());
    assert(level.total_quantity() == 300);
    assert(level.order_count() == 2);
    assert(level.front() == order1);
    assert(level.back() == order2);
    
    // Remove first order
    level.remove_order(order1);
    
    assert(level.total_quantity() == 200);
    assert(level.order_count() == 1);
    assert(level.front() == order2);
    
    // Remove last order
    level.remove_order(order2);
    
    assert(level.empty());
    assert(level.total_quantity() == 0);
    
    pool.release(order1);
    pool.release(order2);
}

TEST(price_level_reduce_quantity) {
    ObjectPool<Order> pool;
    PriceLevel level(1500000);
    
    Order* order = pool.acquire();
    order->order_id = 1;
    order->quantity = 500;
    level.add_order(order);
    
    assert(level.total_quantity() == 500);
    
    // Partial reduction
    level.reduce_quantity(order, 200);
    
    assert(order->quantity == 300);
    assert(level.total_quantity() == 300);
    assert(!level.empty());
    
    // Full reduction (should remove order)
    level.reduce_quantity(order, 300);
    
    assert(order->quantity == 0);
    assert(level.total_quantity() == 0);
    assert(level.empty());
    
    pool.release(order);
}

TEST(price_level_fifo_order) {
    ObjectPool<Order> pool;
    PriceLevel level(1500000);
    
    // Add 5 orders
    std::vector<Order*> orders;
    for (int i = 0; i < 5; ++i) {
        Order* order = pool.acquire();
        order->order_id = i + 1;
        order->quantity = 100;
        orders.push_back(order);
        level.add_order(order);
    }
    
    // Verify FIFO order
    Order* current = level.front();
    for (int i = 0; i < 5; ++i) {
        assert(current != nullptr);
        assert(current->order_id == static_cast<OrderId>(i + 1));
        current = current->next;
    }
    assert(current == nullptr);
    
    // Cleanup
    for (auto* order : orders) {
        level.remove_order(order);
        pool.release(order);
    }
}

// =============================================================================
// Order Book Tests
// =============================================================================

TEST(order_book_add_order) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    // Add buy order
    Order* order = book.add_order(1001, Side::Buy, 1500000, 100, 1000, pool);
    
    assert(order != nullptr);
    assert(order->order_id == 1001);
    assert(order->side == Side::Buy);
    assert(order->price == 1500000);
    assert(order->quantity == 100);
    assert(book.order_count() == 1);
    assert(book.bid_level_count() == 1);
    assert(book.ask_level_count() == 0);
    
    // BBO should be updated
    auto bbo = book.bbo();
    assert(bbo.bid_price == 1500000);
    assert(bbo.bid_quantity == 100);
    assert(bbo.has_bid());
    assert(!bbo.has_ask());
}

TEST(order_book_bbo_updates) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    // Add multiple bid levels
    book.add_order(1, Side::Buy, 1500000, 100, 1000, pool);
    book.add_order(2, Side::Buy, 1499000, 200, 2000, pool);
    book.add_order(3, Side::Buy, 1501000, 150, 3000, pool); // Best bid
    
    // Add multiple ask levels
    book.add_order(4, Side::Sell, 1502000, 100, 4000, pool);
    book.add_order(5, Side::Sell, 1503000, 200, 5000, pool);
    book.add_order(6, Side::Sell, 1501500, 175, 6000, pool); // Best ask
    
    auto bbo = book.bbo();
    assert(bbo.bid_price == 1501000);
    assert(bbo.bid_quantity == 150);
    assert(bbo.ask_price == 1501500);
    assert(bbo.ask_quantity == 175);
    assert(bbo.spread() == 500);
    assert(bbo.midpoint() == 1501250);
}

TEST(order_book_execute_order) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    book.add_order(1001, Side::Buy, 1500000, 500, 1000, pool);
    
    // Partial execution
    Quantity executed = book.execute_order(1001, 200, pool);
    
    assert(executed == 200);
    assert(book.order_count() == 1);
    
    Order* order = book.get_order(1001);
    assert(order != nullptr);
    assert(order->quantity == 300);
    
    // BBO updated
    auto bbo = book.bbo();
    assert(bbo.bid_quantity == 300);
    
    // Full execution
    executed = book.execute_order(1001, 300, pool);
    
    assert(executed == 300);
    assert(book.order_count() == 0);
    assert(book.get_order(1001) == nullptr);
    
    // BBO cleared
    bbo = book.bbo();
    assert(!bbo.has_bid());
}

TEST(order_book_cancel_order) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    book.add_order(1001, Side::Sell, 1510000, 1000, 1000, pool);
    
    // Cancel part
    Quantity cancelled = book.cancel_order(1001, 300, pool);
    
    assert(cancelled == 300);
    assert(book.get_order(1001)->quantity == 700);
    
    // Cancel remaining
    cancelled = book.cancel_order(1001, 700, pool);
    
    assert(cancelled == 700);
    assert(book.get_order(1001) == nullptr);
}

TEST(order_book_delete_order) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    book.add_order(1001, Side::Buy, 1500000, 500, 1000, pool);
    book.add_order(1002, Side::Buy, 1500000, 300, 2000, pool); // Same price level
    
    assert(book.order_count() == 2);
    assert(book.bid_level_count() == 1);
    
    // Delete first order
    bool deleted = book.delete_order(1001, pool);
    
    assert(deleted);
    assert(book.order_count() == 1);
    assert(book.bid_level_count() == 1); // Level still exists
    assert(book.get_order(1001) == nullptr);
    assert(book.get_order(1002) != nullptr);
    
    // Delete second order (should remove level)
    deleted = book.delete_order(1002, pool);
    
    assert(deleted);
    assert(book.order_count() == 0);
    assert(book.bid_level_count() == 0);
    
    // Delete non-existent order
    deleted = book.delete_order(9999, pool);
    
    assert(!deleted);
}

TEST(order_book_replace_order) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    book.add_order(1001, Side::Buy, 1500000, 500, 1000, pool);
    
    // Replace with new price and quantity
    Order* new_order = book.replace_order(1001, 1002, 750, 1505000, 2000, pool);
    
    assert(new_order != nullptr);
    assert(new_order->order_id == 1002);
    assert(new_order->price == 1505000);
    assert(new_order->quantity == 750);
    assert(new_order->side == Side::Buy); // Side preserved
    
    assert(book.get_order(1001) == nullptr);
    assert(book.get_order(1002) != nullptr);
    assert(book.order_count() == 1);
    
    // BBO updated
    auto bbo = book.bbo();
    assert(bbo.bid_price == 1505000);
    assert(bbo.bid_quantity == 750);
}

TEST(order_book_market_depth) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    // Add 5 bid levels
    for (int i = 0; i < 5; ++i) {
        Price price = 1500000 - i * 1000;
        Quantity qty = 100 * (i + 1);
        book.add_order(i + 1, Side::Buy, price, qty, i * 1000, pool);
    }
    
    // Add 5 ask levels
    for (int i = 0; i < 5; ++i) {
        Price price = 1501000 + i * 1000;
        Quantity qty = 150 * (i + 1);
        book.add_order(10 + i, Side::Sell, price, qty, 10000 + i * 1000, pool);
    }
    
    // Get bid depth
    auto bids = book.bid_depth(3);
    assert(bids.size() == 3);
    assert(bids[0].price == 1500000);
    assert(bids[0].quantity == 100);
    assert(bids[1].price == 1499000);
    assert(bids[1].quantity == 200);
    assert(bids[2].price == 1498000);
    assert(bids[2].quantity == 300);
    
    // Get ask depth
    auto asks = book.ask_depth(3);
    assert(asks.size() == 3);
    assert(asks[0].price == 1501000);
    assert(asks[0].quantity == 150);
    assert(asks[1].price == 1502000);
    assert(asks[1].quantity == 300);
    assert(asks[2].price == 1503000);
    assert(asks[2].quantity == 450);
}

TEST(order_book_duplicate_order_id) {
    ObjectPool<Order> pool;
    OrderBook book(1);
    
    Order* order1 = book.add_order(1001, Side::Buy, 1500000, 100, 1000, pool);
    Order* order2 = book.add_order(1001, Side::Buy, 1500000, 200, 2000, pool);
    
    assert(order1 != nullptr);
    assert(order2 == nullptr); // Duplicate should fail
    assert(book.order_count() == 1);
}

// =============================================================================
// Order Book Manager Tests
// =============================================================================

TEST(book_manager_get_book) {
    OrderBookManager manager;
    
    auto& book1 = manager.get_book(1);
    auto& book2 = manager.get_book(2);
    auto& book1_again = manager.get_book(1);
    
    // Same book returned for same locate
    assert(&book1 == &book1_again);
    assert(&book1 != &book2);
    
    assert(manager.has_book(1));
    assert(manager.has_book(2));
    assert(!manager.has_book(3));
}

TEST(book_manager_total_count) {
    OrderBookManager manager;
    
    auto& pool = manager.order_pool();
    
    manager.get_book(1).add_order(1, Side::Buy, 1000000, 100, 1, pool);
    manager.get_book(1).add_order(2, Side::Buy, 1000000, 100, 2, pool);
    manager.get_book(2).add_order(3, Side::Sell, 1000000, 100, 3, pool);
    
    assert(manager.total_order_count() == 3);
    
    manager.clear();
    
    assert(manager.total_order_count() == 0);
}

// =============================================================================
// Symbol Directory Tests
// =============================================================================

TEST(symbol_directory) {
    SymbolDirectory dir;
    
    dir.add_symbol(1, "AAPL    ", 'Q', 'N');
    dir.add_symbol(2, "GOOGL   ", 'Q', 'N');
    dir.add_symbol(3, "MSFT    ", 'Q', 'N');
    
    assert(dir.symbol_count() == 3);
    
    auto* info = dir.get_info(1);
    assert(info != nullptr);
    assert(std::strncmp(info->symbol.data, "AAPL    ", 8) == 0);
    assert(info->market_category == 'Q');
    
    Symbol sym;
    std::memcpy(sym.data, "GOOGL   ", 8);
    auto locate = dir.get_locate(sym);
    assert(locate.has_value());
    assert(locate.value() == 2);
    
    Symbol unknown;
    std::memcpy(unknown.data, "UNKNOWN ", 8);
    assert(!dir.get_locate(unknown).has_value());
}

// =============================================================================
// Order Struct Tests
// =============================================================================

TEST(order_alignment) {
    // Verify Order is cache-line aligned
    assert(sizeof(Order) == 64);
    assert(alignof(Order) >= 64);
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "Running Order Book Tests\n";
    std::cout << std::string(40, '=') << "\n";
    
    // Object pool tests
    std::cout << "\nObject Pool Tests:\n";
    RUN_TEST(object_pool_acquire_release);
    RUN_TEST(object_pool_growth);
    
    // Price level tests
    std::cout << "\nPrice Level Tests:\n";
    RUN_TEST(price_level_add_remove);
    RUN_TEST(price_level_reduce_quantity);
    RUN_TEST(price_level_fifo_order);
    
    // Order book tests
    std::cout << "\nOrder Book Tests:\n";
    RUN_TEST(order_book_add_order);
    RUN_TEST(order_book_bbo_updates);
    RUN_TEST(order_book_execute_order);
    RUN_TEST(order_book_cancel_order);
    RUN_TEST(order_book_delete_order);
    RUN_TEST(order_book_replace_order);
    RUN_TEST(order_book_market_depth);
    RUN_TEST(order_book_duplicate_order_id);
    
    // Order book manager tests
    std::cout << "\nOrder Book Manager Tests:\n";
    RUN_TEST(book_manager_get_book);
    RUN_TEST(book_manager_total_count);
    
    // Symbol directory tests
    std::cout << "\nSymbol Directory Tests:\n";
    RUN_TEST(symbol_directory);
    
    // Structure tests
    std::cout << "\nStructure Tests:\n";
    RUN_TEST(order_alignment);
    
    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "All order book tests PASSED!\n";
    
    return 0;
}
