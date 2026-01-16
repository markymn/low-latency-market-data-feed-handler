/**
 * @file itch_parser.hpp
 * @brief Zero-Copy ITCH 5.0 Protocol Parser
 * 
 * High-performance parser using reinterpret_cast for zero-copy message access.
 * Supports callback-based processing for all message types.
 * 
 * Features:
 * - Zero-copy message deserialization
 * - Template-based message dispatch
 * - Compile-time message type mapping
 * - Statistics tracking with minimal overhead
 */

#ifndef ITCH_PARSER_HPP
#define ITCH_PARSER_HPP

#include "common.hpp"
#include "message_types.hpp"
#include <cstddef>
#include <functional>
#include <array>
#include <atomic>

namespace itch {

// =============================================================================
// Utils
// =============================================================================



// =============================================================================
// Message Handler Interface
// =============================================================================

/**
 * @brief Base message handler interface with virtual dispatch
 * 
 * Implement this interface to receive parsed messages.
 * For maximum performance, consider using the template-based handler.
 */
class MessageHandler {
public:
    virtual ~MessageHandler() = default;
    
    // System Messages
    virtual void on_system_event(const SystemEventMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_stock_directory(const StockDirectoryMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_stock_trading_action(const StockTradingActionMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_reg_sho_restriction(const RegSHORestrictionMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_market_participant_pos(const MarketParticipantPosMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_mwcb_decline_level(const MWCBDeclineLevelMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_mwcb_status(const MWCBStatusMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_ipo_quoting_period(const IPOQuotingPeriodMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_luld_auction_collar(const LULDAuctionCollarMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_operational_halt(const OperationalHaltMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    
    // Order Messages
    virtual void on_add_order(const AddOrderMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_add_order_mpid(const AddOrderMPIDMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_order_executed(const OrderExecutedMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_order_executed_price(const OrderExecutedPriceMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_order_cancel(const OrderCancelMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_order_delete(const OrderDeleteMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_order_replace(const OrderReplaceMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    
    // Trade Messages
    virtual void on_trade(const TradeMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_cross_trade(const CrossTradeMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_broken_trade(const BrokenTradeMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    
    // Auction Messages
    virtual void on_noii(const NOIIMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    virtual void on_rpii(const RPIIMessage& msg, Timestamp ts) { (void)msg; (void)ts; }
    
    // Error handling
    virtual void on_parse_error(const char* data, std::size_t len, const char* error) { 
        (void)data; (void)len; (void)error; 
    }
};

// =============================================================================
// Parser Statistics
// =============================================================================

/**
 * @brief Parser statistics for monitoring and debugging
 */
struct ParserStats {
    std::uint64_t messages_parsed = 0;
    std::uint64_t bytes_processed = 0;
    std::uint64_t parse_errors = 0;
    std::array<std::uint64_t, 256> message_type_counts = {}; // Indexed by message type char
    
    void reset() noexcept {
        messages_parsed = 0;
        bytes_processed = 0;
        parse_errors = 0;
        message_type_counts.fill(0);
    }
};

// =============================================================================
// ITCH 5.0 Parser
// =============================================================================

/**
 * @brief High-performance zero-copy ITCH 5.0 parser
 */
class ITCHParser {
public:
    explicit ITCHParser(MessageHandler* handler = nullptr) noexcept
        : handler_(handler) {}
    
    void set_handler(MessageHandler* handler) noexcept {
        handler_ = handler;
    }
    
    ITCH_FORCE_INLINE std::size_t parse_message(const char* data, std::size_t max_len) noexcept {
        if (ITCH_UNLIKELY(max_len == 0)) {
            return 0;
        }
        
        const char msg_type = data[0];
        const std::size_t msg_size = get_message_size(msg_type);
        
        if (ITCH_UNLIKELY(msg_size == 0)) {
            ++stats_.parse_errors;
            if (handler_) handler_->on_parse_error(data, max_len, "Unknown message type");
            return 1;
        }
        
        if (ITCH_UNLIKELY(max_len < msg_size)) {
            return 0;
        }
        
        const Timestamp ts = extract_timestamp(data);
        dispatch_message(msg_type, data, ts);
        
        ++stats_.messages_parsed;
        stats_.bytes_processed += msg_size;
        ++stats_.message_type_counts[static_cast<unsigned char>(msg_type)];
        
        return msg_size;
    }
    
    std::size_t parse(const char* data, std::size_t len) noexcept {
        std::size_t offset = 0;
        while (offset < len) {
            const std::size_t consumed = parse_message(data + offset, len - offset);
            if (consumed == 0) break;
            offset += consumed;
        }
        return offset;
    }
    
    std::size_t parse_moldudp64(const char* data, std::size_t len) noexcept {
         constexpr std::size_t HEADER_SIZE = 20; 
         if (len < HEADER_SIZE) return 0;
         
         const std::uint16_t msg_count = endian::be16_to_host(*reinterpret_cast<const std::uint16_t*>(data + 18));
         std::size_t offset = HEADER_SIZE;
         std::size_t messages_parsed = 0;
         
         for (std::uint16_t i = 0; i < msg_count && offset + 2 <= len; ++i) {
             const std::uint16_t msg_len = endian::be16_to_host(*reinterpret_cast<const std::uint16_t*>(data + offset));
             offset += 2;
             if (offset + msg_len > len) break;
             
             const std::size_t consumed = parse_message(data + offset, msg_len);
             if (consumed > 0) ++messages_parsed;
             offset += msg_len;
         }
         return messages_parsed;
    }

    const ParserStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

private:
    MessageHandler* handler_;
    ParserStats stats_;
    
    ITCH_FORCE_INLINE static Timestamp extract_timestamp(const char* data) noexcept {
        return endian::be48_to_host(reinterpret_cast<const std::uint8_t*>(data + 5));
    }
    
    ITCH_FORCE_INLINE void dispatch_message(char msg_type, const char* data, Timestamp ts) noexcept {
        if (ITCH_UNLIKELY(!handler_)) return;
        switch (msg_type) {
            case 'S': handler_->on_system_event(*reinterpret_cast<const SystemEventMessage*>(data), ts); break;
            case 'R': handler_->on_stock_directory(*reinterpret_cast<const StockDirectoryMessage*>(data), ts); break;
            case 'H': handler_->on_stock_trading_action(*reinterpret_cast<const StockTradingActionMessage*>(data), ts); break;
            case 'Y': handler_->on_reg_sho_restriction(*reinterpret_cast<const RegSHORestrictionMessage*>(data), ts); break;
            case 'L': handler_->on_market_participant_pos(*reinterpret_cast<const MarketParticipantPosMessage*>(data), ts); break;
            case 'V': handler_->on_mwcb_decline_level(*reinterpret_cast<const MWCBDeclineLevelMessage*>(data), ts); break;
            case 'W': handler_->on_mwcb_status(*reinterpret_cast<const MWCBStatusMessage*>(data), ts); break;
            case 'K': handler_->on_ipo_quoting_period(*reinterpret_cast<const IPOQuotingPeriodMessage*>(data), ts); break;
            case 'J': handler_->on_luld_auction_collar(*reinterpret_cast<const LULDAuctionCollarMessage*>(data), ts); break;
            case 'h': handler_->on_operational_halt(*reinterpret_cast<const OperationalHaltMessage*>(data), ts); break;
            case 'A': handler_->on_add_order(*reinterpret_cast<const AddOrderMessage*>(data), ts); break;
            case 'F': handler_->on_add_order_mpid(*reinterpret_cast<const AddOrderMPIDMessage*>(data), ts); break;
            case 'E': handler_->on_order_executed(*reinterpret_cast<const OrderExecutedMessage*>(data), ts); break;
            case 'C': handler_->on_order_executed_price(*reinterpret_cast<const OrderExecutedPriceMessage*>(data), ts); break;
            case 'X': handler_->on_order_cancel(*reinterpret_cast<const OrderCancelMessage*>(data), ts); break;
            case 'D': handler_->on_order_delete(*reinterpret_cast<const OrderDeleteMessage*>(data), ts); break;
            case 'U': handler_->on_order_replace(*reinterpret_cast<const OrderReplaceMessage*>(data), ts); break;
            case 'P': handler_->on_trade(*reinterpret_cast<const TradeMessage*>(data), ts); break;
            case 'Q': handler_->on_cross_trade(*reinterpret_cast<const CrossTradeMessage*>(data), ts); break;
            case 'B': handler_->on_broken_trade(*reinterpret_cast<const BrokenTradeMessage*>(data), ts); break;
            case 'I': handler_->on_noii(*reinterpret_cast<const NOIIMessage*>(data), ts); break;
            case 'N': handler_->on_rpii(*reinterpret_cast<const RPIIMessage*>(data), ts); break;
            default: handler_->on_parse_error(data, get_message_size(msg_type), "Unhandled message type"); break;
        }
    }
};

// =============================================================================
// Template-Based Parser (Maximum Performance)
// =============================================================================

/**
 * @brief Template-based parser for maximum performance (no virtual dispatch)
 */
template<typename Handler>
class TemplateParser {
public:
    TemplateParser(Handler* handler) : handler_(handler) {}

    ITCH_FORCE_INLINE std::size_t parse_message(const char* data, std::size_t max_len) noexcept {
        if (ITCH_UNLIKELY(max_len == 0)) return 0;
        
        const char msg_type = data[0];
        const std::size_t msg_size = get_message_size(msg_type);
        
        if (ITCH_UNLIKELY(msg_size == 0 || max_len < msg_size)) return 0;
        
        const Timestamp ts = endian::be48_to_host(reinterpret_cast<const std::uint8_t*>(data + 5));
        
        dispatch(msg_type, data, ts);
        
        return msg_size;
    }
    
    std::size_t parse(const char* data, std::size_t len) noexcept {
        std::size_t offset = 0;
        while (offset < len) {
            const std::size_t consumed = parse_message(data + offset, len - offset);
            if (consumed == 0) break;
            offset += consumed;
        }
        return offset;
    }

    std::size_t parse_moldudp64(const char* data, std::size_t len) noexcept {
         constexpr std::size_t HEADER_SIZE = 20; 
         if (len < HEADER_SIZE) return 0;
         
         const std::uint16_t msg_count = endian::be16_to_host(*reinterpret_cast<const std::uint16_t*>(data + 18));
         std::size_t offset = HEADER_SIZE;
         std::size_t messages_parsed = 0;
         
         for (std::uint16_t i = 0; i < msg_count && offset + 2 <= len; ++i) {
             const std::uint16_t msg_len = endian::be16_to_host(*reinterpret_cast<const std::uint16_t*>(data + offset));
             offset += 2;
             if (offset + msg_len > len) break;
             
             const std::size_t consumed = parse_message(data + offset, msg_len);
             if (consumed > 0) ++messages_parsed;
             offset += msg_len;
         }
         return messages_parsed;
    }

    const ParserStats& stats() const noexcept { return stats_; }

    void reset_stats() noexcept { stats_.reset(); }

private:
    Handler* handler_;
    ParserStats stats_;

    ITCH_FORCE_INLINE void dispatch(char msg_type, const char* data, Timestamp ts) noexcept {
        switch (msg_type) {
            case 'A': handler_->on_add_order(*reinterpret_cast<const AddOrderMessage*>(data), ts); break;
            case 'F': handler_->on_add_order_mpid(*reinterpret_cast<const AddOrderMPIDMessage*>(data), ts); break;
            case 'E': handler_->on_order_executed(*reinterpret_cast<const OrderExecutedMessage*>(data), ts); break;
            case 'C': handler_->on_order_executed_price(*reinterpret_cast<const OrderExecutedPriceMessage*>(data), ts); break;
            case 'X': handler_->on_order_cancel(*reinterpret_cast<const OrderCancelMessage*>(data), ts); break;
            case 'D': handler_->on_order_delete(*reinterpret_cast<const OrderDeleteMessage*>(data), ts); break;
            case 'U': handler_->on_order_replace(*reinterpret_cast<const OrderReplaceMessage*>(data), ts); break;
            case 'P': handler_->on_trade(*reinterpret_cast<const TradeMessage*>(data), ts); break;
            case 'Q': handler_->on_cross_trade(*reinterpret_cast<const CrossTradeMessage*>(data), ts); break;
            case 'B': handler_->on_broken_trade(*reinterpret_cast<const BrokenTradeMessage*>(data), ts); break;
            case 'S': handler_->on_system_event(*reinterpret_cast<const SystemEventMessage*>(data), ts); break;
            case 'R': handler_->on_stock_directory(*reinterpret_cast<const StockDirectoryMessage*>(data), ts); break;
            case 'H': handler_->on_stock_trading_action(*reinterpret_cast<const StockTradingActionMessage*>(data), ts); break;
            case 'Y': handler_->on_reg_sho_restriction(*reinterpret_cast<const RegSHORestrictionMessage*>(data), ts); break;
            case 'L': handler_->on_market_participant_pos(*reinterpret_cast<const MarketParticipantPosMessage*>(data), ts); break;
            case 'V': handler_->on_mwcb_decline_level(*reinterpret_cast<const MWCBDeclineLevelMessage*>(data), ts); break;
            case 'W': handler_->on_mwcb_status(*reinterpret_cast<const MWCBStatusMessage*>(data), ts); break;
            case 'K': handler_->on_ipo_quoting_period(*reinterpret_cast<const IPOQuotingPeriodMessage*>(data), ts); break;
            case 'J': handler_->on_luld_auction_collar(*reinterpret_cast<const LULDAuctionCollarMessage*>(data), ts); break;
            case 'h': handler_->on_operational_halt(*reinterpret_cast<const OperationalHaltMessage*>(data), ts); break;
            case 'I': handler_->on_noii(*reinterpret_cast<const NOIIMessage*>(data), ts); break;
            case 'N': handler_->on_rpii(*reinterpret_cast<const RPIIMessage*>(data), ts); break;
            default: break;
        }
    }
};

} // namespace itch

#endif // ITCH_PARSER_HPP
