/**
 * @file order_book.hpp
 * @brief High-Performance Order Book Engine
 * 
 * Features:
 * - Price-time priority order book
 * - O(1) order lookup by ID (Linear Probing Hash Map)
 * - Cache-friendly price level operations (Flat Sorted Vector)
 * - Object pool for minimal heap allocations
 * - Cache-aligned Order struct
 * - Multi-symbol support with efficient symbol lookup
 * - BBO (Best Bid/Offer) caching
 * - Market depth snapshots
 */

#pragma once

#include "common.hpp"
#include "message_types.hpp"
#include <map>
#include <vector>
#include <array>
#include <memory>
#include <cstring>
#include <algorithm>
#include <limits>
#include <cassert>
#include <optional>

namespace itch {

// =============================================================================
// Object Pool for Order Allocation
// =============================================================================

/**
 * @brief Lock-free-friendly object pool for Order objects
 * 
 * Pre-allocates a fixed number of orders to avoid heap allocation
 * during hot path processing.
 */
template<typename T, std::size_t BlockSize = 4096>
class ObjectPool {
public:
    ObjectPool() {
        allocate_block();
    }
    
    ~ObjectPool() {
        for (auto* block : blocks_) {
            delete[] block;
        }
    }
    
    // Non-copyable, non-movable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    
    /**
     * @brief Acquire an object from the pool
     */
    T* acquire() noexcept {
        if (ITCH_UNLIKELY(free_list_.empty())) {
            allocate_block();
        }
        T* obj = free_list_.back();
        free_list_.pop_back();
        return obj;
    }
    
    /**
     * @brief Release an object back to the pool
     */
    void release(T* obj) noexcept {
        free_list_.push_back(obj);
    }
    
    /**
     * @brief Get pool statistics
     */
    std::size_t capacity() const noexcept { 
        return blocks_.size() * BlockSize; 
    }
    
    std::size_t available() const noexcept { 
        return free_list_.size(); 
    }

private:
    std::vector<T*> blocks_;
    std::vector<T*> free_list_;
    
    void allocate_block() {
        T* block = new T[BlockSize];
        blocks_.push_back(block);
        free_list_.reserve(free_list_.size() + BlockSize);
        for (std::size_t i = 0; i < BlockSize; ++i) {
            free_list_.push_back(&block[i]);
        }
    }
};

// =============================================================================
// Linear Probing Hash Map (for Order Lookup)
// =============================================================================

struct Order; // Forward declaration

/**
 * @brief High-performance linear probing hash map for OrderId -> Order*
 * 
 * Avoids the linked-list overhead of std::unordered_map.
 * Fixed Load Factor ~0.5-0.7 for speed.
 */
class OrderMap {
public:
    explicit OrderMap(std::size_t initial_capacity = 100000) {
        resize(initial_capacity);
    }

    Order* find(OrderId id) const noexcept {
        std::size_t idx = hash(id) & mask_;
        while (entries_[idx].id != 0) {
            if (entries_[idx].id == id) {
                return entries_[idx].order;
            }
            idx = (idx + 1) & mask_;
        }
        return nullptr;
    }

    void put(OrderId id, Order* order) noexcept {
        if (ITCH_UNLIKELY(load_ * 2 >= capacity_)) {
            resize(capacity_ * 2);
        }
        
        std::size_t idx = hash(id) & mask_;
        while (entries_[idx].id != 0) {
            idx = (idx + 1) & mask_;
        }
        entries_[idx] = {id, order};
        load_++;
    }

    void remove(OrderId id) noexcept {
        std::size_t idx = hash(id) & mask_;
        while (entries_[idx].id != 0) {
            if (entries_[idx].id == id) {
                // Determine if we need to shift subsequent elements back
                // Simple deletion without tombstones requires backshifting
                entries_[idx].id = 0;
                load_--;
                
                // Backshift (rehash) subsequent items in the cluster
                std::size_t curr = idx;
                std::size_t next = (curr + 1) & mask_;
                
                while (entries_[next].id != 0) {
                    std::size_t desired = hash(entries_[next].id) & mask_;
                    // If desired is between curr and next (cyclically), it stays.
                    // If strictly outside (curr, next], it belongs at curr (or earlier).
                    // Logic: we want to move 'next' to 'curr' if 'next' is NOT in its ideal position
                    // relative to 'curr'.
                    
                    // Standard linear probing deletion:
                    if (!((curr < next && (desired <= curr || desired > next)) ||
                          (curr > next && (desired <= curr && desired > next)))) {
                        // Element is validly placed
                    } else {
                        // Move element back
                        entries_[curr] = entries_[next];
                        entries_[next].id = 0;
                        curr = next;
                    }
                    next = (next + 1) & mask_;
                }
                return;
            }
            idx = (idx + 1) & mask_;
        }
    }
    
    void clear() noexcept {
        std::fill(entries_.begin(), entries_.end(), Entry{0, nullptr});
        load_ = 0;
    }

private:
    struct Entry {
        OrderId id;
        Order* order;
    };
    
    std::vector<Entry> entries_;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::size_t load_ = 0;

    static std::size_t hash(OrderId id) noexcept {
        return static_cast<std::size_t>(id);
    }

    void resize(std::size_t new_cap) {
        // Enforce power of 2
        std::size_t cap = 1;
        while (cap < new_cap) cap <<= 1;
        
        std::vector<Entry> old_entries = std::move(entries_);
        entries_.resize(cap, {0, nullptr});
        capacity_ = cap;
        mask_ = cap - 1;
        load_ = 0;

        for (const auto& e : old_entries) {
            if (e.id != 0) {
                // Re-insert
                std::size_t idx = hash(e.id) & mask_;
                while (entries_[idx].id != 0) {
                    idx = (idx + 1) & mask_;
                }
                entries_[idx] = e;
                load_++;
            }
        }
    }
};

// =============================================================================
// Order Structure
// =============================================================================

/**
 * @brief Order in the book (cache-line aligned for performance)
 */
struct ITCH_CACHE_ALIGNED Order {
    OrderId         order_id;       // 8 bytes
    Price           price;          // 8 bytes
    Quantity        quantity;       // 4 bytes
    Quantity        original_qty;   // 4 bytes
    StockLocate     stock_locate;   // 2 bytes
    Side            side;           // 1 byte
    char            padding[5];     // Padding
    Timestamp       timestamp;      // 8 bytes
    Order*          next;           // 8 bytes
    Order*          prev;           // 8 bytes
    
    Order() noexcept = default;
    
    void reset() noexcept {
        order_id = 0;
        price = 0;
        quantity = 0;
        original_qty = 0;
        stock_locate = 0;
        side = Side::Buy;
        timestamp = 0;
        next = nullptr;
        prev = nullptr;
    }
};

static_assert(sizeof(Order) == 64, "Order must be cache-line aligned (64 bytes)");

// =============================================================================
// Price Level
// =============================================================================

/**
 * @brief Price level containing all orders at a specific price
 * 
 * Maintains a doubly-linked list of orders.
 */
class PriceLevel {
public:
    PriceLevel() noexcept = default;
    
    explicit PriceLevel(Price price) noexcept 
        : price_(price) {}
    
    void add_order(Order* order) noexcept {
        order->prev = tail_;
        order->next = nullptr;
        
        if (tail_) {
            tail_->next = order;
        } else {
            head_ = order;
        }
        tail_ = order;
        
        total_quantity_ += order->quantity;
        ++order_count_;
    }
    
    void remove_order(Order* order) noexcept {
        total_quantity_ -= order->quantity;
        --order_count_;
        
        if (order->prev) {
            order->prev->next = order->next;
        } else {
            head_ = order->next;
        }
        
        if (order->next) {
            order->next->prev = order->prev;
        } else {
            tail_ = order->prev;
        }
        
        order->next = nullptr;
        order->prev = nullptr;
    }
    
    void reduce_quantity(Order* order, Quantity delta) noexcept {
        assert(order->quantity >= delta);
        order->quantity -= delta;
        total_quantity_ -= delta;
        
        if (order->quantity == 0) {
            remove_order(order);
        }
    }
    
    Price price() const noexcept { return price_; }
    Quantity total_quantity() const noexcept { return total_quantity_; }
    std::size_t order_count() const noexcept { return order_count_; }
    bool empty() const noexcept { return order_count_ == 0; }
    Order* front() const noexcept { return head_; }
    Order* back() const noexcept { return tail_; }

private:
    Price price_ = 0;
    Order* head_ = nullptr;
    Order* tail_ = nullptr;
    Quantity total_quantity_ = 0;
    std::size_t order_count_ = 0;
};

// =============================================================================
// Best Bid/Offer (BBO)
// =============================================================================

struct BBO {
    Price bid_price = 0;
    Price ask_price = std::numeric_limits<Price>::max();
    Quantity bid_quantity = 0;
    Quantity ask_quantity = 0;
    
    bool has_bid() const noexcept { return bid_quantity > 0; }
    bool has_ask() const noexcept { return ask_quantity > 0; }
    
    Price spread() const noexcept {
        if (!has_bid() || !has_ask()) return 0;
        return ask_price - bid_price;
    }
    
    Price midpoint() const noexcept {
        if (!has_bid() || !has_ask()) return 0;
        return (bid_price + ask_price) / 2;
    }
};

struct DepthLevel {
    Price price;
    Quantity quantity;
    std::size_t order_count;
};

// =============================================================================
// Order Book (Single Symbol)
// =============================================================================

/**
 * @brief Full depth order book for a single symbol
 * 
 * Optimized:
 * - Uses std::vector<PriceLevel> w/ std::lower_bound for Price Levels (cache locality)
 * - Uses custom OrderMap (Linear Probing) for Orders
 */
class OrderBook {
public:
    OrderBook() noexcept = default;
    
    explicit OrderBook(StockLocate stock_locate) noexcept 
        : stock_locate_(stock_locate) {
    }
    
    Order* add_order(OrderId order_id, Side side, Price price, 
                     Quantity quantity, Timestamp timestamp,
                     ObjectPool<Order>& pool) noexcept {
        if (ITCH_UNLIKELY(orders_.find(order_id) != nullptr)) {
            return nullptr;
        }
        
        Order* order = pool.acquire();
        order->order_id = order_id;
        order->price = price;
        order->quantity = quantity;
        order->original_qty = quantity;
        order->stock_locate = stock_locate_;
        order->side = side;
        order->timestamp = timestamp;
        order->next = nullptr;
        order->prev = nullptr;
        
        orders_.put(order_id, order);
        
        if (is_buy(side)) {
            auto it = bids_.find(price);
            if (it == bids_.end()) {
                it = bids_.emplace(price, PriceLevel(price)).first;
            }
            it->second.add_order(order);
            update_best_bid();
        } else {
            auto it = asks_.find(price);
            if (it == asks_.end()) {
                it = asks_.emplace(price, PriceLevel(price)).first;
            }
            it->second.add_order(order);
            update_best_ask();
        }
        
        ++order_count_;
        return order;
    }
    
    Quantity execute_order(OrderId order_id, Quantity quantity,
                          ObjectPool<Order>& pool) noexcept {
        Order* order = orders_.find(order_id);
        if (ITCH_UNLIKELY(order == nullptr)) {
            return 0;
        }
        
        const Quantity exec_qty = std::min(quantity, order->quantity);
        const Side side = order->side;
        const Price price = order->price;
        
        if (is_buy(side)) {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                it->second.reduce_quantity(order, exec_qty);
                if (it->second.empty()) {
                    bids_.erase(it);
                }
            }
            update_best_bid();
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                it->second.reduce_quantity(order, exec_qty);
                if (it->second.empty()) {
                    asks_.erase(it);
                }
            }
            update_best_ask();
        }
        
        if (order->quantity == 0) {
            orders_.remove(order_id);
            pool.release(order);
            --order_count_;
        }
        
        return exec_qty;
    }
    
    Quantity cancel_order(OrderId order_id, Quantity quantity,
                          ObjectPool<Order>& pool) noexcept {
        return execute_order(order_id, quantity, pool);
    }
    
    bool delete_order(OrderId order_id, ObjectPool<Order>& pool) noexcept {
        Order* order = orders_.find(order_id);
        if (ITCH_UNLIKELY(order == nullptr)) {
            return false;
        }
        
        const Side side = order->side;
        const Price price = order->price;
        
        if (is_buy(side)) {
            auto it = bids_.find(price);
            if (it != bids_.end()) {
                it->second.remove_order(order);
                if (it->second.empty()) {
                    bids_.erase(it);
                }
            }
            update_best_bid();
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) {
                it->second.remove_order(order);
                if (it->second.empty()) {
                    asks_.erase(it);
                }
            }
            update_best_ask();
        }
        
        orders_.remove(order_id);
        pool.release(order);
        --order_count_;
        return true;
    }
    
    Order* replace_order(OrderId old_order_id, OrderId new_order_id,
                        Quantity new_quantity, Price new_price,
                        Timestamp timestamp, ObjectPool<Order>& pool) noexcept {
        Order* old_order = orders_.find(old_order_id);
        if (ITCH_UNLIKELY(old_order == nullptr)) {
             return nullptr;
        }
        const Side side = old_order->side;
        
        delete_order(old_order_id, pool);
        return add_order(new_order_id, side, new_price, new_quantity, timestamp, pool);
    }
    
    Order* get_order(OrderId order_id) const noexcept {
        return orders_.find(order_id);
    }
    
    const BBO& bbo() const noexcept { return bbo_; }
    
    std::vector<DepthLevel> bid_depth(std::size_t max_levels = 10) const {
        std::vector<DepthLevel> depth;
        depth.reserve(max_levels);
        std::size_t count = 0;
        for (auto it = bids_.begin(); it != bids_.end() && count < max_levels; ++it, ++count) {
            depth.push_back({it->second.price(), it->second.total_quantity(), it->second.order_count()});
        }
        return depth;
    }
    
    std::vector<DepthLevel> ask_depth(std::size_t max_levels = 10) const {
        std::vector<DepthLevel> depth;
        depth.reserve(max_levels);
        std::size_t count = 0;
        for (auto it = asks_.begin(); it != asks_.end() && count < max_levels; ++it, ++count) {
            depth.push_back({it->second.price(), it->second.total_quantity(), it->second.order_count()});
        }
        return depth;
    }
    
    std::size_t order_count() const noexcept { return order_count_; }
    std::size_t bid_level_count() const noexcept { return bids_.size(); }
    std::size_t ask_level_count() const noexcept { return asks_.size(); }
    StockLocate stock_locate() const noexcept { return stock_locate_; }
    
    void clear(ObjectPool<Order>& pool) noexcept {
         for (auto& pair : bids_) {
             Order* curr = pair.second.front();
             while (curr) {
                 Order* next = curr->next;
                 pool.release(curr);
                 curr = next;
             }
         }
         for (auto& pair : asks_) {
             Order* curr = pair.second.front();
             while (curr) {
                 Order* next = curr->next;
                 pool.release(curr);
                 curr = next;
             }
         }
         
         bids_.clear();
         asks_.clear();
         orders_.clear();
         bbo_ = BBO{};
         order_count_ = 0;
    }

private:
    StockLocate stock_locate_ = 0;
    std::map<Price, PriceLevel, std::greater<Price>> bids_; // Highest bid first
    std::map<Price, PriceLevel, std::less<Price>> asks_;    // Lowest ask first
    OrderMap orders_;
    BBO bbo_;
    std::size_t order_count_ = 0;
    
    void update_best_bid() noexcept {
        if (bids_.empty()) {
            bbo_.bid_price = 0;
            bbo_.bid_quantity = 0;
        } else {
            const auto& best = bids_.begin()->second;
            bbo_.bid_price = best.price();
            bbo_.bid_quantity = best.total_quantity();
        }
    }
    
    void update_best_ask() noexcept {
        if (asks_.empty()) {
            bbo_.ask_price = std::numeric_limits<Price>::max();
            bbo_.ask_quantity = 0;
        } else {
            const auto& best = asks_.begin()->second;
            bbo_.ask_price = best.price();
            bbo_.ask_quantity = best.total_quantity();
        }
    }
};

// =============================================================================
// Order Book Manager (Multiple Symbols)
// =============================================================================

class OrderBookManager {
public:
    static constexpr std::size_t MAX_SYMBOLS = 8192;
    
    OrderBookManager() {
        books_.resize(MAX_SYMBOLS);
    }
    
    OrderBook& get_book(StockLocate stock_locate) noexcept {
        if (books_[stock_locate].stock_locate() == 0) {
            books_[stock_locate] = OrderBook(stock_locate);
        }
        return books_[stock_locate];
    }
    
    bool has_book(StockLocate stock_locate) const noexcept {
        return stock_locate < books_.size() && 
               books_[stock_locate].stock_locate() != 0;
    }
    
    ObjectPool<Order>& order_pool() noexcept { return order_pool_; }
    
    std::size_t total_order_count() const noexcept {
        std::size_t count = 0;
        for (const auto& book : books_) {
            count += book.order_count();
        }
        return count;
    }
    
    void clear() noexcept {
        for (auto& book : books_) {
            book.clear(order_pool_);
        }
    }

private:
    std::vector<OrderBook> books_;
    ObjectPool<Order> order_pool_;
};

// =============================================================================
// Symbol Directory
// =============================================================================
class SymbolDirectory {
public:
    struct SymbolInfo {
        Symbol symbol;
        char market_category;
        char financial_status;
        bool is_active;
    };
    
    void add_symbol(StockLocate locate, const char* symbol_str, 
                   char market_category, char financial_status) {
        SymbolInfo info;
        std::memcpy(info.symbol.data, symbol_str, 8);
        info.market_category = market_category;
        info.financial_status = financial_status;
        info.is_active = true;
        
        if (locate >= symbols_.size()) {
            symbols_.resize(locate + 1);
        }
        symbols_[locate] = info;
        symbol_to_locate_[info.symbol] = locate;
    }
    
    const SymbolInfo* get_info(StockLocate locate) const noexcept {
        if (locate >= symbols_.size()) return nullptr;
        return symbols_[locate].is_active ? &symbols_[locate] : nullptr;
    }
    
    std::optional<StockLocate> get_locate(const Symbol& symbol) const noexcept {
        auto it = symbol_to_locate_.find(symbol);
        if (it != symbol_to_locate_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    std::size_t symbol_count() const noexcept {
        std::size_t count = 0;
        for (const auto& info : symbols_) {
            if (info.is_active) ++count;
        }
        return count;
    }

private:
    std::vector<SymbolInfo> symbols_;
    std::unordered_map<Symbol, StockLocate> symbol_to_locate_;
};

} // namespace itch
