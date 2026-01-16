/**
 * @file main.cpp
 * @brief ITCH 5.0 Feed Handler - Benchmarks and Example Usage
 * 
 * Demonstrates:
 * - Processing ITCH messages
 * - Order book updates
 * - Performance benchmarking
 * - Market depth queries
 */

#include "../include/feed_handler.hpp"

#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>
#include <thread>
#include <sstream>
#include <cstring>

namespace {

// =============================================================================
// Test Data Generator
// =============================================================================

/**
 * @brief Generates synthetic ITCH messages for testing
 */
class ITCHMessageGenerator {
public:
    ITCHMessageGenerator(std::uint32_t seed = 42) 
        : rng_(seed), price_dist_(1000, 100000), qty_dist_(100, 10000) {}
    
    /**
     * @brief Generate a stock directory message
     */
    void generate_stock_directory(char* buffer, itch::StockLocate locate, 
                                  const char* symbol) {
        auto* msg = reinterpret_cast<itch::StockDirectoryMessage*>(buffer);
        msg->message_type = 'R';
        set_be16(msg->stock_locate, locate);
        set_be16(msg->tracking_number, 0);
        set_timestamp(msg->timestamp, current_timestamp_++);
        std::memset(msg->stock, ' ', 8);
        std::memcpy(msg->stock, symbol, std::min<std::size_t>(8, std::strlen(symbol)));
        msg->market_category = 'Q';
        msg->financial_status = 'N';
        set_be32(msg->round_lot_size, 100);
        msg->round_lots_only = 'N';
        msg->issue_classification = 'C';
        msg->issue_subtype[0] = 'Z';
        msg->issue_subtype[1] = ' ';
        msg->authenticity = 'P';
        msg->short_sale_threshold = ' ';
        msg->ipo_flag = ' ';
        msg->luld_ref_price_tier = ' ';
        msg->etp_flag = ' ';
        set_be32(msg->etp_leverage_factor, 0);
        msg->inverse_indicator = 'N';
    }
    
    /**
     * @brief Generate an add order message
     */
    void generate_add_order(char* buffer, itch::StockLocate locate,
                           itch::OrderId order_id, bool is_buy,
                           itch::Price price, itch::Quantity qty) {
        auto* msg = reinterpret_cast<itch::AddOrderMessage*>(buffer);
        msg->message_type = 'A';
        set_be16(msg->stock_locate, locate);
        set_be16(msg->tracking_number, 0);
        set_timestamp(msg->timestamp, current_timestamp_++);
        set_be64(msg->order_ref_number, order_id);
        msg->buy_sell_indicator = is_buy ? 'B' : 'S';
        set_be32(msg->shares, qty);
        std::memset(msg->stock, ' ', 8);
        set_be32(msg->price, static_cast<std::uint32_t>(price));
    }
    
    /**
     * @brief Generate a random add order message
     */
    void generate_random_add_order(char* buffer, itch::StockLocate locate) {
        bool is_buy = std::uniform_int_distribution<int>(0, 1)(rng_) == 0;
        itch::Price price = price_dist_(rng_);
        itch::Quantity qty = qty_dist_(rng_);
        generate_add_order(buffer, locate, next_order_id_++, is_buy, price, qty);
    }
    
    /**
     * @brief Generate an order executed message
     */
    void generate_order_executed(char* buffer, itch::StockLocate locate,
                                itch::OrderId order_id, itch::Quantity qty) {
        auto* msg = reinterpret_cast<itch::OrderExecutedMessage*>(buffer);
        msg->message_type = 'E';
        set_be16(msg->stock_locate, locate);
        set_be16(msg->tracking_number, 0);
        set_timestamp(msg->timestamp, current_timestamp_++);
        set_be64(msg->order_ref_number, order_id);
        set_be32(msg->executed_shares, qty);
        set_be64(msg->match_number, next_match_id_++);
    }
    
    /**
     * @brief Generate an order cancel message
     */
    void generate_order_cancel(char* buffer, itch::StockLocate locate,
                              itch::OrderId order_id, itch::Quantity qty) {
        auto* msg = reinterpret_cast<itch::OrderCancelMessage*>(buffer);
        msg->message_type = 'X';
        set_be16(msg->stock_locate, locate);
        set_be16(msg->tracking_number, 0);
        set_timestamp(msg->timestamp, current_timestamp_++);
        set_be64(msg->order_ref_number, order_id);
        set_be32(msg->cancelled_shares, qty);
    }
    
    /**
     * @brief Generate an order delete message
     */
    void generate_order_delete(char* buffer, itch::StockLocate locate,
                              itch::OrderId order_id) {
        auto* msg = reinterpret_cast<itch::OrderDeleteMessage*>(buffer);
        msg->message_type = 'D';
        set_be16(msg->stock_locate, locate);
        set_be16(msg->tracking_number, 0);
        set_timestamp(msg->timestamp, current_timestamp_++);
        set_be64(msg->order_ref_number, order_id);
    }
    
    /**
     * @brief Generate an order replace message
     */
    void generate_order_replace(char* buffer, itch::StockLocate locate,
                               itch::OrderId old_order_id, itch::OrderId new_order_id,
                               itch::Quantity qty, itch::Price price) {
        auto* msg = reinterpret_cast<itch::OrderReplaceMessage*>(buffer);
        msg->message_type = 'U';
        set_be16(msg->stock_locate, locate);
        set_be16(msg->tracking_number, 0);
        set_timestamp(msg->timestamp, current_timestamp_++);
        set_be64(msg->original_order_ref_number, old_order_id);
        set_be64(msg->new_order_ref_number, new_order_id);
        set_be32(msg->shares, qty);
        set_be32(msg->price, static_cast<std::uint32_t>(price));
    }
    
    /**
     * @brief Generate a realistic add order message (Random Walk / Clustering)
     */
    void generate_realistic_add_order(char* buffer, itch::StockLocate locate, 
                                      itch::OrderId order_id) {
        // Initialize state for this symbol if needed
        if (symbol_prices_.size() <= locate) {
            symbol_prices_.resize(locate + 1, 1500000); // Default 150.0000
        }
        
        itch::Price& ref_price = symbol_prices_[locate];
        
        // Random walk: -0.01 to +0.01 change
        std::uniform_int_distribution<int> walk(-100, 100);
        ref_price += walk(rng_);
        if (ref_price < 100) ref_price = 100;
        
        // Determine side (roughly 50/50)
        bool is_buy = std::uniform_int_distribution<int>(0, 1)(rng_) == 0;
        
        // Place order near BBO (tight spread)
        // Spread offset 0-5 cents
        int spread_offset = std::uniform_int_distribution<int>(0, 500)(rng_); 
        
        itch::Price price;
        if (is_buy) {
            price = ref_price - spread_offset;
        } else {
            price = ref_price + spread_offset;
        }
        
        itch::Quantity qty = qty_dist_(rng_);
        generate_add_order(buffer, locate, order_id, is_buy, price, qty);
    }
    
    itch::OrderId next_order_id() const { return next_order_id_; }
    void set_next_order_id(itch::OrderId id) { next_order_id_ = id; }

private:
    std::mt19937_64 rng_;
    std::uniform_int_distribution<itch::Price> price_dist_;
    std::uniform_int_distribution<itch::Quantity> qty_dist_;
    std::vector<itch::Price> symbol_prices_; // Tracks current 'price' for each symbol
    
    itch::Timestamp current_timestamp_ = 34200000000000ULL; // 9:30 AM in nanoseconds
    itch::OrderId next_order_id_ = 1;
    std::uint64_t next_match_id_ = 1;
    
    static void set_be16(std::uint16_t& field, std::uint16_t value) {
        field = itch::endian::be16_to_host(value);
    }
    
    static void set_be32(std::uint32_t& field, std::uint32_t value) {
        field = itch::endian::be32_to_host(value);
    }
    
    static void set_be64(std::uint64_t& field, std::uint64_t value) {
        field = itch::endian::be64_to_host(value);
    }
    
    static void set_timestamp(std::uint8_t* ts, itch::Timestamp value) {
        ts[0] = static_cast<std::uint8_t>((value >> 40) & 0xFF);
        ts[1] = static_cast<std::uint8_t>((value >> 32) & 0xFF);
        ts[2] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
        ts[3] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        ts[4] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        ts[5] = static_cast<std::uint8_t>(value & 0xFF);
    }
};

// =============================================================================
// Example Event Handler
// =============================================================================

class ExampleEventHandler : public itch::FeedEventHandler {
public:
    void on_trade(const itch::TradeEvent& event) override {
        ++trade_count_;
        if (verbose_) {
            std::cout << "TRADE: locate=" << event.stock_locate
                      << " price=" << format_price(event.price)
                      << " qty=" << event.quantity
                      << " side=" << (event.side == itch::Side::Buy ? "BUY" : "SELL")
                      << "\n";
        }
    }
    
    void on_bbo_update(const itch::BBOEvent& event) override {
        ++bbo_update_count_;
        if (verbose_) {
            std::cout << "BBO: locate=" << event.stock_locate
                      << " bid=" << format_price(event.new_bbo.bid_price)
                      << "x" << event.new_bbo.bid_quantity
                      << " ask=" << format_price(event.new_bbo.ask_price)
                      << "x" << event.new_bbo.ask_quantity
                      << " spread=" << format_price(event.new_bbo.spread())
                      << "\n";
        }
    }
    
    void on_symbol_added(itch::StockLocate locate, const itch::Symbol& symbol) override {
        ++symbol_count_;
        if (verbose_) {
            std::cout << "SYMBOL: locate=" << locate
                      << " symbol=" << std::string(symbol.data, 8)
                      << "\n";
        }
    }
    
    void set_verbose(bool verbose) { verbose_ = verbose; }
    
    std::uint64_t trade_count() const { return trade_count_; }
    std::uint64_t bbo_update_count() const { return bbo_update_count_; }
    std::uint64_t symbol_count() const { return symbol_count_; }
    
    void reset() {
        trade_count_ = 0;
        bbo_update_count_ = 0;
        symbol_count_ = 0;
    }

private:
    std::uint64_t trade_count_ = 0;
    std::uint64_t bbo_update_count_ = 0;
    std::uint64_t symbol_count_ = 0;
    bool verbose_ = false;
    
    static std::string format_price(itch::Price price) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4) << (price / 10000.0);
        return oss.str();
    }
};

// =============================================================================
// Utility Functions
// =============================================================================

void print_separator() {
    std::cout << std::string(70, '=') << "\n";
}

void print_header(const std::string& title) {
    print_separator();
    std::cout << " " << title << "\n";
    print_separator();
}

std::string format_number(std::uint64_t num) {
    std::ostringstream oss;
    oss << num;
    return oss.str();
}

} // anonymous namespace

// =============================================================================
// Main Program
// =============================================================================

int main(int argc, char* argv[]) {
    std::cout << R"(
  ___ _____ ____ _   _   ____   ___    _____             _ 
 |_ _|_   _/ ___| | | | | ___| / _ \  |  ___|__  ___  __| |
  | |  | || |   | |_| | |___ \| | | | | |_ / _ \/ _ \/ _` |
  | |  | || |___|  _  |  ___) | |_| | |  _|  __/  __/ (_| |
 |___| |_| \____|_| |_| |____(_)___/  |_|  \___|\___|\__,_|
                                                           
  High-Performance NASDAQ ITCH 5.0 Market Data Handler
)" << "\n";
    
    // =========================================================================
    // Example 1: Basic Usage
    // =========================================================================
    
    print_header("Example 1: Basic Usage");
    
    itch::FeedHandler handler;
    ExampleEventHandler event_handler;
    event_handler.set_verbose(true);
    handler.set_event_handler(&event_handler);
    
    // Generate some test messages
    ITCHMessageGenerator gen;
    char buffer[64];
    
    // Add stock directory
    gen.generate_stock_directory(buffer, 1, "AAPL");
    handler.process(buffer, sizeof(itch::StockDirectoryMessage));
    
    // Add some orders
    gen.generate_add_order(buffer, 1, 1001, true, 1500000, 100);  // Buy @ 150.0000
    handler.process(buffer, sizeof(itch::AddOrderMessage));
    
    gen.generate_add_order(buffer, 1, 1002, true, 1499000, 200);  // Buy @ 149.9000
    handler.process(buffer, sizeof(itch::AddOrderMessage));
    
    gen.generate_add_order(buffer, 1, 1003, false, 1501000, 150); // Sell @ 150.1000
    handler.process(buffer, sizeof(itch::AddOrderMessage));
    
    gen.generate_add_order(buffer, 1, 1004, false, 1502000, 250); // Sell @ 150.2000
    handler.process(buffer, sizeof(itch::AddOrderMessage));
    
    // Query the order book
    std::cout << "\n--- Order Book State ---\n";
    auto& book = handler.book_manager().get_book(1);
    auto bbo = book.bbo();
    
    std::cout << "BBO: " << (bbo.bid_price / 10000.0) << " x " << bbo.bid_quantity
              << " / " << (bbo.ask_price / 10000.0) << " x " << bbo.ask_quantity << "\n";
    std::cout << "Spread: " << (bbo.spread() / 10000.0) << "\n";
    std::cout << "Midpoint: " << (bbo.midpoint() / 10000.0) << "\n";
    
    std::cout << "\nBid Depth:\n";
    for (const auto& level : book.bid_depth(5)) {
        std::cout << "  " << (level.price / 10000.0) << " x " << level.quantity
                  << " (" << level.order_count << " orders)\n";
    }
    
    std::cout << "\nAsk Depth:\n";
    for (const auto& level : book.ask_depth(5)) {
        std::cout << "  " << (level.price / 10000.0) << " x " << level.quantity
                  << " (" << level.order_count << " orders)\n";
    }
    
    // Execute an order
    std::cout << "\n--- Executing order 1001 (50 shares) ---\n";
    gen.generate_order_executed(buffer, 1, 1001, 50);
    handler.process(buffer, sizeof(itch::OrderExecutedMessage));
    
    // =========================================================================
    // Example 2: Performance Benchmark
    // =========================================================================
    
    print_header("Example 2: Performance Benchmark");
    
    event_handler.set_verbose(false);
    event_handler.reset();
    handler.reset();
    handler.enable_metrics(true);
    
    constexpr std::size_t NUM_SYMBOLS = 100;
    constexpr std::size_t NUM_ORDERS_PER_SYMBOL = 10000;
    constexpr std::size_t TOTAL_ORDERS = NUM_SYMBOLS * NUM_ORDERS_PER_SYMBOL;
    
    std::cout << "Generating " << format_number(TOTAL_ORDERS) << " orders "
              << "across " << NUM_SYMBOLS << " symbols...\n";
    
    // Pre-generate all messages
    std::vector<char> message_buffer(TOTAL_ORDERS * sizeof(itch::AddOrderMessage));
    
    // Add symbols first
    for (std::size_t i = 0; i < NUM_SYMBOLS; ++i) {
        char symbol[9];
        std::snprintf(symbol, sizeof(symbol), "SYM%05zu", i);
        gen.generate_stock_directory(buffer, static_cast<itch::StockLocate>(i + 1), symbol);
        handler.process(buffer, sizeof(itch::StockDirectoryMessage));
    }
    
    // Generate order messages
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<std::size_t> symbol_dist(1, NUM_SYMBOLS);
    
    for (std::size_t i = 0; i < TOTAL_ORDERS; ++i) {
        itch::StockLocate locate = static_cast<itch::StockLocate>(symbol_dist(rng));
        gen.generate_realistic_add_order(
            message_buffer.data() + i * sizeof(itch::AddOrderMessage),
            locate,
            i + 1
        );
    }
    
    // Benchmark: Process all messages
    std::cout << "Processing messages...\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (std::size_t i = 0; i < TOTAL_ORDERS; ++i) {
        handler.process(
            message_buffer.data() + i * sizeof(itch::AddOrderMessage),
            sizeof(itch::AddOrderMessage)
        );
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    auto elapsed_ms = elapsed_ns / 1e6;
    
    double msgs_per_sec = static_cast<double>(TOTAL_ORDERS) / (elapsed_ns / 1e9);
    double ns_per_msg = static_cast<double>(elapsed_ns) / TOTAL_ORDERS;
    
    std::cout << "\n--- Benchmark Results ---\n";
    std::cout << "Messages processed: " << format_number(TOTAL_ORDERS) << "\n";
    std::cout << "Elapsed time: " << std::fixed << std::setprecision(2) 
              << elapsed_ms << " ms\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(0) 
              << msgs_per_sec << " msgs/sec\n";
    std::cout << "             " << std::fixed << std::setprecision(2)
              << (msgs_per_sec / 1e6) << " M msgs/sec\n";
    std::cout << "Latency per message: " << std::fixed << std::setprecision(1)
              << ns_per_msg << " ns\n";

    {
        std::ofstream out("results.txt");
        out << "Performance: " << std::fixed << std::setprecision(2) 
            << (msgs_per_sec / 1e6) << " M messages/second\n";
        out << "Latency: " << std::fixed << std::setprecision(0) 
            << ns_per_msg << " ns\n";
    }
    
    const auto& metrics = handler.metrics();
    std::cout << "\n--- Feed Metrics ---\n";
    std::cout << "Orders added: " << format_number(metrics.orders_added) << "\n";
    std::cout << "BBO updates: " << format_number(event_handler.bbo_update_count()) << "\n";
    std::cout << "Total orders in books: " 
              << format_number(handler.book_manager().total_order_count()) << "\n";
    
    if (metrics.book_update_latency.count() > 0) {
        std::cout << "\n--- Book Update Latency ---\n";
        std::cout << "Min: " << metrics.book_update_latency.min() << " ns\n";
        std::cout << "Mean: " << std::fixed << std::setprecision(1) 
                  << metrics.book_update_latency.mean() << " ns\n";
        std::cout << "P50: " << metrics.book_update_latency.p50() << " ns\n";
        std::cout << "P99: " << metrics.book_update_latency.p99() << " ns\n";
        std::cout << "P99.9: " << metrics.book_update_latency.p999() << " ns\n";
        std::cout << "Max: " << metrics.book_update_latency.max() << " ns\n";
    }
    
    // =========================================================================
    // Example 3: Multi-symbol Market Depth
    // =========================================================================
    
    print_header("Example 3: Multi-Symbol Order Books");
    
    std::cout << "Sample order books (first 5 symbols):\n\n";
    
    for (itch::StockLocate locate = 1; locate <= 5; ++locate) {
        auto& sym_book = handler.book_manager().get_book(locate);
        auto sym_bbo = sym_book.bbo();
        
        std::cout << "Symbol " << locate << ": ";
        if (sym_bbo.has_bid() && sym_bbo.has_ask()) {
            std::cout << std::fixed << std::setprecision(4)
                      << (sym_bbo.bid_price / 10000.0) << " x " << sym_bbo.bid_quantity
                      << " / "
                      << (sym_bbo.ask_price / 10000.0) << " x " << sym_bbo.ask_quantity
                      << " (spread: " << (sym_bbo.spread() / 10000.0) << ")";
        } else {
            std::cout << "No market";
        }
        std::cout << " [" << sym_book.order_count() << " orders, "
                  << sym_book.bid_level_count() << " bid levels, "
                  << sym_book.ask_level_count() << " ask levels]\n";
    }
    
    // =========================================================================
    // Example 4: Symbol Filtering
    // =========================================================================
    
    print_header("Example 4: Symbol Filtering");
    
    handler.reset();
    event_handler.reset();
    
    // Only process symbols 1-3
    std::set<itch::StockLocate> filter = {1, 2, 3};
    handler.set_symbol_filter(filter);
    
    std::cout << "Processing with filter (symbols 1-3 only)...\n";
    
    // Process some orders for various symbols
    for (itch::StockLocate locate = 1; locate <= 10; ++locate) {
        gen.generate_add_order(buffer, locate, 
                              gen.next_order_id() + locate, 
                              true, 1500000 + locate * 1000, 100);
        gen.set_next_order_id(gen.next_order_id() + 1);
        handler.process(buffer, sizeof(itch::AddOrderMessage));
    }
    
    std::cout << "Orders in filtered books:\n";
    for (itch::StockLocate locate = 1; locate <= 5; ++locate) {
        auto& sym_book = handler.book_manager().get_book(locate);
        std::cout << "  Symbol " << locate << ": " 
                  << sym_book.order_count() << " orders\n";
    }
    
    handler.clear_symbol_filter();
    
    // =========================================================================
    // Summary
    // =========================================================================
    
    print_header("Summary");
    
    std::cout << "ITCH 5.0 Feed Handler successfully demonstrated:\n";
    std::cout << "  - Zero-copy message parsing\n";
    std::cout << "  - Order book management with price-time priority\n";
    std::cout << "  - BBO calculation and market depth queries\n";
    std::cout << "  - Multi-symbol support\n";
    std::cout << "  - Symbol filtering\n";
    std::cout << "  - Performance benchmarking\n";
    std::cout << "\nPerformance: " << std::fixed << std::setprecision(2)
              << (msgs_per_sec / 1e6) << " M messages/second\n";
    std::cout << "Latency: " << std::fixed << std::setprecision(0)
              << ns_per_msg << " ns per message\n";
    
    print_separator();
    
    return 0;
}
