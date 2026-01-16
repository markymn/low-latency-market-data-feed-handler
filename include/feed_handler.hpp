/**
 * @file feed_handler.hpp
 * @brief ITCH 5.0 Feed Handler
 * 
 * Integrates the parser and order book engine into a complete
 * market data processing system.
 * 
 * Features:
 * - Symbol filtering
 * - Event callbacks for trades, BBO updates, depth changes
 * - Performance metrics (latency, throughput)
 * - Memory-mapped file support for replay
 * - Cache Warming (HFT Pattern)
 * - Template-based Dispatch (Zero virtual calls)
 */

#pragma once

#include "common.hpp"
#include "message_types.hpp"
#include "itch_parser.hpp"
#include "order_book.hpp"

#include <functional>
#include <fstream>
#include <vector>
#include <set>
#include <atomic>
#include <algorithm>
#include <numeric>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace itch {

// =============================================================================
// Performance Metrics
// =============================================================================

/**
 * @brief Latency histogram for performance measurement
 */
class LatencyHistogram {
public:
    static constexpr std::size_t NUM_BUCKETS = 100;
    static constexpr std::uint64_t BUCKET_SIZE_NS = 100; // 100ns per bucket
    
    void record(std::uint64_t latency_ns) noexcept {
        std::size_t bucket = std::min(latency_ns / BUCKET_SIZE_NS, NUM_BUCKETS - 1);
        ++buckets_[bucket];
        ++count_;
        total_ += latency_ns;
        min_ = std::min(min_, latency_ns);
        max_ = std::max(max_, latency_ns);
    }
    
    void reset() noexcept {
        buckets_.fill(0);
        count_ = 0;
        total_ = 0;
        min_ = std::numeric_limits<std::uint64_t>::max();
        max_ = 0;
    }
    
    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t min() const noexcept { return min_; }
    std::uint64_t max() const noexcept { return max_; }
    
    double mean() const noexcept {
        return count_ > 0 ? static_cast<double>(total_) / count_ : 0.0;
    }
    
    std::uint64_t percentile(double p) const noexcept {
        if (count_ == 0) return 0;
        
        std::uint64_t target = static_cast<std::uint64_t>(count_ * p);
        std::uint64_t cumulative = 0;
        
        for (std::size_t i = 0; i < NUM_BUCKETS; ++i) {
            cumulative += buckets_[i];
            if (cumulative >= target) {
                return i * BUCKET_SIZE_NS;
            }
        }
        return (NUM_BUCKETS - 1) * BUCKET_SIZE_NS;
    }
    
    std::uint64_t p50() const noexcept { return percentile(0.50); }
    std::uint64_t p99() const noexcept { return percentile(0.99); }
    std::uint64_t p999() const noexcept { return percentile(0.999); }

private:
    std::array<std::uint64_t, NUM_BUCKETS> buckets_ = {};
    std::uint64_t count_ = 0;
    std::uint64_t total_ = 0;
    std::uint64_t min_ = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_ = 0;
};

/**
 * @brief Feed handler performance metrics
 */
struct FeedMetrics {
    std::uint64_t messages_processed = 0;
    std::uint64_t orders_added = 0;
    std::uint64_t orders_executed = 0;
    std::uint64_t orders_cancelled = 0;
    std::uint64_t orders_deleted = 0;
    std::uint64_t orders_replaced = 0;
    std::uint64_t trades = 0;
    std::uint64_t bbo_updates = 0;
    
    LatencyHistogram parse_latency;
    LatencyHistogram book_update_latency;
    
    // Throughput tracking
    std::uint64_t start_time_ns = 0;
    std::uint64_t last_report_time_ns = 0;
    std::uint64_t messages_since_last_report = 0;
    
    void reset() noexcept {
        messages_processed = 0;
        orders_added = 0;
        orders_executed = 0;
        orders_cancelled = 0;
        orders_deleted = 0;
        orders_replaced = 0;
        trades = 0;
        bbo_updates = 0;
        parse_latency.reset();
        book_update_latency.reset();
        start_time_ns = 0;
        last_report_time_ns = 0;
        messages_since_last_report = 0;
    }
    
    double throughput_mps() const noexcept {
        if (start_time_ns == 0) return 0.0;
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count() - start_time_ns;
        if (elapsed_ns == 0) return 0.0;
        return static_cast<double>(messages_processed) / (elapsed_ns / 1e9);
    }
};

// =============================================================================
// Event Callbacks
// =============================================================================

struct TradeEvent {
    StockLocate stock_locate;
    Price price;
    Quantity quantity;
    OrderId order_ref;
    std::uint64_t match_number;
    Side side;
    Timestamp timestamp;
};

struct BBOEvent {
    StockLocate stock_locate;
    BBO old_bbo;
    BBO new_bbo;
    Timestamp timestamp;
};

class FeedEventHandler {
public:
    virtual ~FeedEventHandler() = default;
    
    virtual void on_trade(const TradeEvent& event) { (void)event; }
    virtual void on_bbo_update(const BBOEvent& event) { (void)event; }
    virtual void on_symbol_added(StockLocate locate, const Symbol& symbol) { 
        (void)locate; (void)symbol; 
    }
};

// =============================================================================
// Memory-Mapped File Reader
// =============================================================================

class MemoryMappedFile {
public:
    MemoryMappedFile() = default;
    
    ~MemoryMappedFile() {
        close();
    }
    
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;
    
    MemoryMappedFile(MemoryMappedFile&& other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
#ifdef _WIN32
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
    }
    
    bool open(const char* path) {
#ifdef _WIN32
        file_handle_ = CreateFileA(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );
        
        if (file_handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file_handle_, &file_size)) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        size_ = static_cast<std::size_t>(file_size.QuadPart);
        
        mapping_handle_ = CreateFileMappingA(
            file_handle_,
            nullptr,
            PAGE_READONLY,
            0, 0,
            nullptr
        );
        
        if (mapping_handle_ == nullptr) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        
        data_ = static_cast<const char*>(MapViewOfFile(
            mapping_handle_,
            FILE_MAP_READ,
            0, 0,
            0
        ));
        
        if (data_ == nullptr) {
            CloseHandle(mapping_handle_);
            CloseHandle(file_handle_);
            mapping_handle_ = nullptr;
            file_handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        
        return true;
#else
        fd_ = ::open(path, O_RDONLY);
        if (fd_ < 0) {
            return false;
        }
        
        struct stat st;
        if (fstat(fd_, &st) < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        size_ = static_cast<std::size_t>(st.st_size);
        
        data_ = static_cast<const char*>(mmap(
            nullptr,
            size_,
            PROT_READ,
            MAP_PRIVATE,
            fd_,
            0
        ));
        
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        madvise(const_cast<char*>(data_), size_, MADV_SEQUENTIAL);
        return true;
#endif
    }
    
    void close() {
#ifdef _WIN32
        if (data_) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_handle_) {
            CloseHandle(mapping_handle_);
            mapping_handle_ = nullptr;
        }
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (data_) {
            munmap(const_cast<char*>(data_), size_);
            data_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        size_ = 0;
    }
    
    bool is_open() const noexcept { return data_ != nullptr; }
    const char* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }

private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;
    
#ifdef _WIN32
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

// =============================================================================
// ITCH 5.0 Feed Handler
// =============================================================================

/**
 * @brief Complete ITCH 5.0 feed handler
 * 
 * Performance Optimized:
 * - Uses TemplateParser (no virtuals)
 * - Cache Warming Support
 */
class FeedHandler {
public:
    FeedHandler() : parser_(this) {}
    
    void set_event_handler(FeedEventHandler* handler) noexcept {
        event_handler_ = handler;
    }
    
    void enable_metrics(bool enable) noexcept {
        collect_metrics_ = enable;
        if (enable) {
            metrics_.reset();
            auto now = std::chrono::high_resolution_clock::now();
            metrics_.start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count();
        }
    }
    
    void set_symbol_filter(const std::set<StockLocate>& locates) {
        symbol_filter_ = locates;
        use_filter_ = !locates.empty();
    }
    
    void clear_symbol_filter() {
        symbol_filter_.clear();
        use_filter_ = false;
    }
    
    // Cache Warming (from HFT Paper)
    void warmup() {
        // Touch order pool to fault pages
        std::vector<Order*> temp_orders;
        temp_orders.reserve(10000);
        for(int i=0; i<10000; ++i) {
            temp_orders.push_back(book_manager_.order_pool().acquire());
        }
        for(auto* o : temp_orders) {
            o->price = 1; // write access
            book_manager_.order_pool().release(o);
        }
        // Pre-initialize basic books
        book_manager_.get_book(1);
    }
    
    std::size_t process(const char* data, std::size_t len) {
        return parser_.parse(data, len);
    }
    
    std::size_t process_moldudp64(const char* data, std::size_t len) {
        return parser_.parse_moldudp64(data, len);
    }
    
    std::size_t process_file(const char* path) {
        MemoryMappedFile file;
        if (!file.open(path)) {
            return 0;
        }
        return process(file.data(), file.size());
    }
    
    OrderBookManager& book_manager() noexcept { return book_manager_; }
    const OrderBookManager& book_manager() const noexcept { return book_manager_; }
    
    SymbolDirectory& symbol_directory() noexcept { return symbol_directory_; }
    const SymbolDirectory& symbol_directory() const noexcept { return symbol_directory_; }
    
    const FeedMetrics& metrics() const noexcept { return metrics_; }
    const ParserStats& parser_stats() const noexcept { return parser_.stats(); }
    
    void reset() {
        book_manager_.clear();
        parser_.reset_stats();
        metrics_.reset();
    }

// Handlers called by TemplateParser (implicitly)
public:
    void on_system_event(const SystemEventMessage&, Timestamp) {
        ++metrics_.messages_processed;
    }
    void on_stock_directory(const StockDirectoryMessage& msg, Timestamp ts) {
        (void)ts;
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        symbol_directory_.add_symbol(locate, msg.stock, msg.market_category, msg.financial_status);
        if (event_handler_) {
            Symbol sym;
            std::memcpy(sym.data, msg.stock, 8);
            event_handler_->on_symbol_added(locate, sym);
        }
        ++metrics_.messages_processed;
    }
    void on_stock_trading_action(const StockTradingActionMessage&, Timestamp) { ++metrics_.messages_processed; }
    void on_reg_sho_restriction(const RegSHORestrictionMessage&, Timestamp) { ++metrics_.messages_processed; }
    void on_market_participant_pos(const MarketParticipantPosMessage&, Timestamp) { ++metrics_.messages_processed; }
    void on_mwcb_decline_level(const MWCBDeclineLevelMessage&, Timestamp) { ++metrics_.messages_processed; }
    void on_mwcb_status(const MWCBStatusMessage&, Timestamp) { ++metrics_.messages_processed; }
    void on_ipo_quoting_period(const IPOQuotingPeriodMessage&, Timestamp) { ++metrics_.messages_processed; }
    void on_luld_auction_collar(const LULDAuctionCollarMessage&, Timestamp) { ++metrics_.messages_processed; }
    void on_operational_halt(const OperationalHaltMessage&, Timestamp) { ++metrics_.messages_processed; }
    
    void on_add_order(const AddOrderMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        std::uint64_t start_cycles = collect_metrics_ ? timing::rdtsc() : 0;
        
        OrderBook& book = book_manager_.get_book(locate);
        // Optimization: Don't get old BBO if no event handler to save copy
        BBO old_bbo;
        if (event_handler_) old_bbo = book.bbo();
        
        OrderId order_id = endian::be64_to_host(msg.order_ref_number);
        Price price = static_cast<Price>(endian::be32_to_host(msg.price));
        Quantity quantity = endian::be32_to_host(msg.shares);
        Side side = char_to_side(msg.buy_sell_indicator);
        
        book.add_order(order_id, side, price, quantity, ts, book_manager_.order_pool());
        
        if (collect_metrics_) {
            std::uint64_t end_cycles = timing::rdtscp();
             metrics_.book_update_latency.record((end_cycles - start_cycles) / 3);
            ++metrics_.orders_added;
            ++metrics_.messages_processed;
        }
        
        if (event_handler_) {
            const BBO& new_bbo = book.bbo();
            if (old_bbo.bid_price != new_bbo.bid_price || old_bbo.ask_price != new_bbo.ask_price) {
                event_handler_->on_bbo_update({locate, old_bbo, new_bbo, ts});
                ++metrics_.bbo_updates;
            }
        }
    }
    
    void on_add_order_mpid(const AddOrderMPIDMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        OrderBook& book = book_manager_.get_book(locate);
        BBO old_bbo;
        if (event_handler_) old_bbo = book.bbo(); // Optimization
        
        OrderId order_id = endian::be64_to_host(msg.order_ref_number);
        Price price = static_cast<Price>(endian::be32_to_host(msg.price));
        Quantity quantity = endian::be32_to_host(msg.shares);
        Side side = char_to_side(msg.buy_sell_indicator);
        
        book.add_order(order_id, side, price, quantity, ts, book_manager_.order_pool());
        
        ++metrics_.orders_added;
        ++metrics_.messages_processed;
        
        if (event_handler_) {
            const BBO& new_bbo = book.bbo();
             if (old_bbo.bid_price != new_bbo.bid_price || old_bbo.ask_price != new_bbo.ask_price) {
                event_handler_->on_bbo_update({locate, old_bbo, new_bbo, ts});
                ++metrics_.bbo_updates;
            }
        }
    }
    
    void on_order_executed(const OrderExecutedMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        OrderBook& book = book_manager_.get_book(locate);
        BBO old_bbo;
        if (event_handler_) old_bbo = book.bbo();
        
        OrderId order_id = endian::be64_to_host(msg.order_ref_number);
        Quantity exec_shares = endian::be32_to_host(msg.executed_shares);
        
        Order* order = book.get_order(order_id);
        if (order) {
            if (event_handler_) {
                event_handler_->on_trade({locate, order->price, exec_shares, order_id, endian::be64_to_host(msg.match_number), order->side, ts});
            }
            book.execute_order(order_id, exec_shares, book_manager_.order_pool());
        }
        
        ++metrics_.orders_executed;
        ++metrics_.trades;
        ++metrics_.messages_processed;
        
        if (event_handler_) {
            const BBO& new_bbo = book.bbo();
             if (old_bbo.bid_price != new_bbo.bid_price || old_bbo.ask_price != new_bbo.ask_price) {
                event_handler_->on_bbo_update({locate, old_bbo, new_bbo, ts});
                ++metrics_.bbo_updates;
            }
        }
    }
    
    void on_order_executed_price(const OrderExecutedPriceMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        OrderBook& book = book_manager_.get_book(locate);
         BBO old_bbo;
        if (event_handler_) old_bbo = book.bbo();
        
        OrderId order_id = endian::be64_to_host(msg.order_ref_number);
        Quantity exec_shares = endian::be32_to_host(msg.executed_shares);
        Price exec_price = static_cast<Price>(endian::be32_to_host(msg.execution_price));
        
        Order* order = book.get_order(order_id);
        if (order) {
            if (event_handler_) {
                event_handler_->on_trade({locate, exec_price, exec_shares, order_id, endian::be64_to_host(msg.match_number), order->side, ts});
            }
            book.execute_order(order_id, exec_shares, book_manager_.order_pool());
        }
        
        ++metrics_.orders_executed;
        ++metrics_.trades;
        ++metrics_.messages_processed;
        
        if (event_handler_) {
             const BBO& new_bbo = book.bbo();
             if (old_bbo.bid_price != new_bbo.bid_price || old_bbo.ask_price != new_bbo.ask_price) {
                event_handler_->on_bbo_update({locate, old_bbo, new_bbo, ts});
                ++metrics_.bbo_updates;
            }
        }
    }
    
    void on_order_cancel(const OrderCancelMessage& msg, Timestamp ts) {
         StockLocate locate = endian::be16_to_host(msg.stock_locate);
         if (use_filter_ && symbol_filter_.count(locate) == 0) return;
         
         OrderBook& book = book_manager_.get_book(locate);
         BBO old_bbo;
        if (event_handler_) old_bbo = book.bbo();
         
         OrderId order_id = endian::be64_to_host(msg.order_ref_number);
         Quantity cancel_shares = endian::be32_to_host(msg.cancelled_shares);
         
         book.cancel_order(order_id, cancel_shares, book_manager_.order_pool());
         
         ++metrics_.orders_cancelled;
         ++metrics_.messages_processed;
         
         if (event_handler_) {
            const BBO& new_bbo = book.bbo();
            if (old_bbo.bid_price != new_bbo.bid_price || old_bbo.ask_price != new_bbo.ask_price) {
                event_handler_->on_bbo_update({locate, old_bbo, new_bbo, ts});
                ++metrics_.bbo_updates;
            }
        }
    }
    
    void on_order_delete(const OrderDeleteMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        OrderBook& book = book_manager_.get_book(locate);
        BBO old_bbo;
        if (event_handler_) old_bbo = book.bbo();
        
        OrderId order_id = endian::be64_to_host(msg.order_ref_number);
        
        book.delete_order(order_id, book_manager_.order_pool());
        
        ++metrics_.orders_deleted;
        ++metrics_.messages_processed;
        
        if (event_handler_) {
            const BBO& new_bbo = book.bbo();
            if (old_bbo.bid_price != new_bbo.bid_price || old_bbo.ask_price != new_bbo.ask_price) {
                event_handler_->on_bbo_update({locate, old_bbo, new_bbo, ts});
                ++metrics_.bbo_updates;
            }
        }
    }
    
    void on_order_replace(const OrderReplaceMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        OrderBook& book = book_manager_.get_book(locate);
        BBO old_bbo;
        if (event_handler_) old_bbo = book.bbo();
        
        OrderId old_order_id = endian::be64_to_host(msg.original_order_ref_number);
        OrderId new_order_id = endian::be64_to_host(msg.new_order_ref_number);
        Quantity new_shares = endian::be32_to_host(msg.shares);
        Price new_price = static_cast<Price>(endian::be32_to_host(msg.price));
        
        book.replace_order(old_order_id, new_order_id, new_shares, new_price, ts, book_manager_.order_pool());
        
        ++metrics_.orders_replaced;
        ++metrics_.messages_processed;
        
         if (event_handler_) {
            const BBO& new_bbo = book.bbo();
            if (old_bbo.bid_price != new_bbo.bid_price || old_bbo.ask_price != new_bbo.ask_price) {
                event_handler_->on_bbo_update({locate, old_bbo, new_bbo, ts});
                ++metrics_.bbo_updates;
            }
        }
    }
    
    void on_trade(const TradeMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        if (event_handler_) {
            event_handler_->on_trade({locate, static_cast<Price>(endian::be32_to_host(msg.price)), endian::be32_to_host(msg.shares), endian::be64_to_host(msg.order_ref_number), endian::be64_to_host(msg.match_number), char_to_side(msg.buy_sell_indicator), ts});
        }
        ++metrics_.trades;
        ++metrics_.messages_processed;
    }
    
    void on_cross_trade(const CrossTradeMessage& msg, Timestamp ts) {
        StockLocate locate = endian::be16_to_host(msg.stock_locate);
        if (use_filter_ && symbol_filter_.count(locate) == 0) return;
        
        if (event_handler_) {
            event_handler_->on_trade({locate, static_cast<Price>(endian::be32_to_host(msg.cross_price)), static_cast<Quantity>(endian::be64_to_host(msg.shares)), 0, endian::be64_to_host(msg.match_number), Side::Buy, ts});
        }
         ++metrics_.trades;
        ++metrics_.messages_processed;
    }
    
    void on_rpii(const RPIIMessage&, Timestamp) {
         ++metrics_.messages_processed;
    }
    void on_noii(const NOIIMessage&, Timestamp) {
         ++metrics_.messages_processed;
    }
    void on_broken_trade(const BrokenTradeMessage&, Timestamp) {
         ++metrics_.messages_processed;
    }
    void on_parse_error(const char*, std::size_t, const char*) {
        if (collect_metrics_) metrics_.parse_latency.record(0); // Dummy
    }

private:
    TemplateParser<FeedHandler> parser_;
    OrderBookManager book_manager_;
    SymbolDirectory symbol_directory_;
    FeedMetrics metrics_;
    FeedEventHandler* event_handler_ = nullptr;
    
    std::set<StockLocate> symbol_filter_;
    bool use_filter_ = false;
    bool collect_metrics_ = false;
};

} // namespace itch
