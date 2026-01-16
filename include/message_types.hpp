/**
 * @file message_types.hpp
 * @brief NASDAQ ITCH 5.0 Protocol Message Definitions
 * 
 * Zero-copy message structures using packed attributes to exactly match
 * the wire format. All fields are big-endian as per ITCH specification.
 * 
 * Reference: NASDAQ TotalView-ITCH 5.0 Specification
 */

#pragma once

#include "common.hpp"
#include <cstdint>
#include <type_traits>

namespace itch {

// =============================================================================
// Message Type Enumeration
// =============================================================================

enum class MessageType : char {
    // System Messages
    SystemEvent             = 'S',
    StockDirectory          = 'R',
    StockTradingAction      = 'H',
    RegSHORestriction       = 'Y',
    MarketParticipantPos    = 'L',
    MWCBDeclineLevel        = 'V',
    MWCBStatus              = 'W',
    IPOQuotingPeriod        = 'K',
    LULDAuctionCollar       = 'J',
    OperationalHalt         = 'h',
    
    // Order Messages
    AddOrder                = 'A',
    AddOrderMPID            = 'F',
    OrderExecuted           = 'E',
    OrderExecutedPrice      = 'C',
    OrderCancel             = 'X',
    OrderDelete             = 'D',
    OrderReplace            = 'U',
    
    // Trade Messages
    Trade                   = 'P',
    CrossTrade              = 'Q',
    BrokenTrade             = 'B',
    
    // Auction Messages
    NOII                    = 'I',
    RPII                    = 'N',
    
    // Unknown/Invalid
    Unknown                 = '\0'
};

/**
 * @brief Fast lookup table for message type validation
 */
ITCH_FORCE_INLINE constexpr bool is_valid_message_type(char c) noexcept {
    switch (c) {
        case 'S': case 'R': case 'H': case 'Y': case 'L':
        case 'V': case 'W': case 'K': case 'J': case 'h':
        case 'A': case 'F': case 'E': case 'C': case 'X':
        case 'D': case 'U': case 'P': case 'Q': case 'B':
        case 'I': case 'N':
            return true;
        default:
            return false;
    }
}

// =============================================================================
// Packed Message Structures (Wire Format)
// =============================================================================

#pragma pack(push, 1)

/**
 * @brief Common header fields present in most messages
 * Note: This is for reference only, not used directly as messages vary
 */
struct MessageHeader {
    char        message_type;       // 1 byte
    std::uint16_t stock_locate;     // 2 bytes (big-endian)
    std::uint16_t tracking_number;  // 2 bytes (big-endian)
    std::uint8_t  timestamp[6];     // 6 bytes (48-bit nanoseconds since midnight)
};

// -----------------------------------------------------------------------------
// System Messages
// -----------------------------------------------------------------------------

/**
 * @brief System Event Message (Type 'S')
 * Signals important system-wide events
 * Size: 12 bytes
 */
struct SystemEventMessage {
    char          message_type;       // 'S'
    std::uint16_t stock_locate;       // Always 0 for system messages
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];       // Nanoseconds since midnight
    char          event_code;         // O=Start, S=Start System Hours, etc.
    
    // Event codes
    static constexpr char EVENT_START_OF_MESSAGES = 'O';
    static constexpr char EVENT_START_SYSTEM_HOURS = 'S';
    static constexpr char EVENT_START_MARKET_HOURS = 'Q';
    static constexpr char EVENT_END_MARKET_HOURS = 'M';
    static constexpr char EVENT_END_SYSTEM_HOURS = 'E';
    static constexpr char EVENT_END_OF_MESSAGES = 'C';
};
static_assert(sizeof(SystemEventMessage) == 12, "SystemEventMessage must be 12 bytes");

/**
 * @brief Stock Directory Message (Type 'R')
 * Provides basic security information
 * Size: 39 bytes
 */
struct StockDirectoryMessage {
    char          message_type;         // 'R'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          stock[8];             // Stock symbol (space-padded)
    char          market_category;      // Q=NASDAQ Global Select, G=NASDAQ Global, etc.
    char          financial_status;     // D=Deficient, E=Delinquent, etc.
    std::uint32_t round_lot_size;
    char          round_lots_only;      // Y/N
    char          issue_classification; // A=American Depositary Share, etc.
    char          issue_subtype[2];
    char          authenticity;         // P=Live/Production, T=Test
    char          short_sale_threshold; // Y/N/ (space = N/A)
    char          ipo_flag;             // Y/N/ (space = N/A)
    char          luld_ref_price_tier;  // 1=Tier 1, 2=Tier 2, (space = N/A)
    char          etp_flag;             // Y/N/ (space = N/A)
    std::uint32_t etp_leverage_factor;
    char          inverse_indicator;    // Y/N
};
static_assert(sizeof(StockDirectoryMessage) == 39, "StockDirectoryMessage must be 39 bytes");

/**
 * @brief Stock Trading Action Message (Type 'H')
 * Indicates trading state for a stock
 * Size: 25 bytes
 */
struct StockTradingActionMessage {
    char          message_type;       // 'H'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          stock[8];
    char          trading_state;      // H=Halted, P=Paused, Q=Quotation Only, T=Trading
    char          reserved;
    char          reason[4];
    
    static constexpr char STATE_HALTED = 'H';
    static constexpr char STATE_PAUSED = 'P';
    static constexpr char STATE_QUOTATION_ONLY = 'Q';
    static constexpr char STATE_TRADING = 'T';
};
static_assert(sizeof(StockTradingActionMessage) == 25, "StockTradingActionMessage must be 25 bytes");

/**
 * @brief Reg SHO Short Sale Price Test Restriction (Type 'Y')
 * Size: 20 bytes
 */
struct RegSHORestrictionMessage {
    char          message_type;       // 'Y'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          stock[8];
    char          reg_sho_action;     // 0=No restriction, 1=Activated, 2=Deactivated
};
static_assert(sizeof(RegSHORestrictionMessage) == 20, "RegSHORestrictionMessage must be 20 bytes");

/**
 * @brief Market Participant Position Message (Type 'L')
 * Size: 26 bytes
 */
struct MarketParticipantPosMessage {
    char          message_type;       // 'L'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          mpid[4];
    char          stock[8];
    char          primary_market_maker;  // Y/N
    char          market_maker_mode;     // N=Normal, P=Passive, S=Syndicate, etc.
    char          market_participant_state; // A=Active, E=Excused, W=Withdrawn, etc.
};
static_assert(sizeof(MarketParticipantPosMessage) == 26, "MarketParticipantPosMessage must be 26 bytes");

/**
 * @brief MWCB Decline Level Message (Type 'V')
 * Market-Wide Circuit Breaker decline levels
 * Size: 35 bytes
 */
struct MWCBDeclineLevelMessage {
    char          message_type;       // 'V'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t level1;             // Price (8 decimal places)
    std::uint64_t level2;
    std::uint64_t level3;
};
static_assert(sizeof(MWCBDeclineLevelMessage) == 35, "MWCBDeclineLevelMessage must be 35 bytes");

/**
 * @brief MWCB Status Message (Type 'W')
 * Size: 12 bytes
 */
struct MWCBStatusMessage {
    char          message_type;       // 'W'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          breached_level;     // 1/2/3
};
static_assert(sizeof(MWCBStatusMessage) == 12, "MWCBStatusMessage must be 12 bytes");

/**
 * @brief IPO Quoting Period Update (Type 'K')
 * Size: 28 bytes
 */
struct IPOQuotingPeriodMessage {
    char          message_type;       // 'K'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          stock[8];
    std::uint32_t ipo_quotation_release_time;
    char          ipo_quotation_release_qualifier; // A=Anticipated, C=Cancelled
    std::uint32_t ipo_price;          // Price (4 decimal places)
};
static_assert(sizeof(IPOQuotingPeriodMessage) == 28, "IPOQuotingPeriodMessage must be 28 bytes");

/**
 * @brief LULD Auction Collar (Type 'J')
 * Size: 35 bytes
 */
struct LULDAuctionCollarMessage {
    char          message_type;       // 'J'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          stock[8];
    std::uint32_t auction_collar_ref_price;
    std::uint32_t upper_auction_collar_price;
    std::uint32_t lower_auction_collar_price;
    std::uint32_t auction_collar_extension;
};
static_assert(sizeof(LULDAuctionCollarMessage) == 35, "LULDAuctionCollarMessage must be 35 bytes");

/**
 * @brief Operational Halt Message (Type 'h')
 * Size: 21 bytes
 */
struct OperationalHaltMessage {
    char          message_type;       // 'h'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          stock[8];
    char          market_code;        // Q=NASDAQ, B=BX, X=PSX
    char          operational_halt_action; // H=Halted, T=Resumed
};
static_assert(sizeof(OperationalHaltMessage) == 21, "OperationalHaltMessage must be 21 bytes");

// -----------------------------------------------------------------------------
// Order Messages
// -----------------------------------------------------------------------------

/**
 * @brief Add Order Message (Type 'A') - No MPID Attribution
 * Size: 36 bytes
 */
struct AddOrderMessage {
    char          message_type;       // 'A'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t order_ref_number;
    char          buy_sell_indicator; // 'B' or 'S'
    std::uint32_t shares;
    char          stock[8];
    std::uint32_t price;              // Price (4 decimal places)
};
static_assert(sizeof(AddOrderMessage) == 36, "AddOrderMessage must be 36 bytes");

/**
 * @brief Add Order with MPID Attribution (Type 'F')
 * Size: 40 bytes
 */
struct AddOrderMPIDMessage {
    char          message_type;       // 'F'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t order_ref_number;
    char          buy_sell_indicator; // 'B' or 'S'
    std::uint32_t shares;
    char          stock[8];
    std::uint32_t price;              // Price (4 decimal places)
    char          attribution[4];     // MPID
};
static_assert(sizeof(AddOrderMPIDMessage) == 40, "AddOrderMPIDMessage must be 40 bytes");

/**
 * @brief Order Executed Message (Type 'E')
 * Size: 31 bytes
 */
struct OrderExecutedMessage {
    char          message_type;       // 'E'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t order_ref_number;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
};
static_assert(sizeof(OrderExecutedMessage) == 31, "OrderExecutedMessage must be 31 bytes");

/**
 * @brief Order Executed with Price Message (Type 'C')
 * Size: 36 bytes
 */
struct OrderExecutedPriceMessage {
    char          message_type;       // 'C'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t order_ref_number;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
    char          printable;          // Y/N
    std::uint32_t execution_price;    // Price (4 decimal places)
};
static_assert(sizeof(OrderExecutedPriceMessage) == 36, "OrderExecutedPriceMessage must be 36 bytes");

/**
 * @brief Order Cancel Message (Type 'X')
 * Size: 23 bytes
 */
struct OrderCancelMessage {
    char          message_type;       // 'X'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t order_ref_number;
    std::uint32_t cancelled_shares;
};
static_assert(sizeof(OrderCancelMessage) == 23, "OrderCancelMessage must be 23 bytes");

/**
 * @brief Order Delete Message (Type 'D')
 * Size: 19 bytes
 */
struct OrderDeleteMessage {
    char          message_type;       // 'D'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t order_ref_number;
};
static_assert(sizeof(OrderDeleteMessage) == 19, "OrderDeleteMessage must be 19 bytes");

/**
 * @brief Order Replace Message (Type 'U')
 * Size: 35 bytes
 */
struct OrderReplaceMessage {
    char          message_type;       // 'U'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t original_order_ref_number;
    std::uint64_t new_order_ref_number;
    std::uint32_t shares;
    std::uint32_t price;              // Price (4 decimal places)
};
static_assert(sizeof(OrderReplaceMessage) == 35, "OrderReplaceMessage must be 35 bytes");

// -----------------------------------------------------------------------------
// Trade Messages
// -----------------------------------------------------------------------------

/**
 * @brief Trade Message (Non-Cross) (Type 'P')
 * Size: 44 bytes
 */
struct TradeMessage {
    char          message_type;       // 'P'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t order_ref_number;
    char          buy_sell_indicator; // 'B' or 'S'
    std::uint32_t shares;
    char          stock[8];
    std::uint32_t price;              // Price (4 decimal places)
    std::uint64_t match_number;
};
static_assert(sizeof(TradeMessage) == 44, "TradeMessage must be 44 bytes");

/**
 * @brief Cross Trade Message (Type 'Q')
 * Size: 40 bytes
 */
struct CrossTradeMessage {
    char          message_type;       // 'Q'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t shares;
    char          stock[8];
    std::uint32_t cross_price;        // Price (4 decimal places)
    std::uint64_t match_number;
    char          cross_type;         // O=Opening, C=Closing, H=Halted/IPO, I=Intraday
};
static_assert(sizeof(CrossTradeMessage) == 40, "CrossTradeMessage must be 40 bytes");

/**
 * @brief Broken Trade Message (Type 'B')
 * Size: 19 bytes
 */
struct BrokenTradeMessage {
    char          message_type;       // 'B'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t match_number;
};
static_assert(sizeof(BrokenTradeMessage) == 19, "BrokenTradeMessage must be 19 bytes");

// -----------------------------------------------------------------------------
// Auction Messages
// -----------------------------------------------------------------------------

/**
 * @brief NOII Message (Type 'I')
 * Net Order Imbalance Indicator
 * Size: 50 bytes
 */
struct NOIIMessage {
    char          message_type;       // 'I'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    std::uint64_t paired_shares;
    std::uint64_t imbalance_shares;
    char          imbalance_direction; // B=Buy, S=Sell, N=No imbalance, O=Insufficient orders
    char          stock[8];
    std::uint32_t far_price;
    std::uint32_t near_price;
    std::uint32_t current_ref_price;
    char          cross_type;         // O=Opening, C=Closing, H=Halted/IPO
    char          price_variation_indicator;
};
static_assert(sizeof(NOIIMessage) == 50, "NOIIMessage must be 50 bytes");

/**
 * @brief RPII Message (Type 'N')
 * Retail Price Improvement Indicator
 * Size: 20 bytes
 */
struct RPIIMessage {
    char          message_type;       // 'N'
    std::uint16_t stock_locate;
    std::uint16_t tracking_number;
    std::uint8_t  timestamp[6];
    char          stock[8];
    char          interest_flag;      // B=Buy, S=Sell, A=Both, N=None
};
static_assert(sizeof(RPIIMessage) == 20, "RPIIMessage must be 20 bytes");

#pragma pack(pop)

// =============================================================================
// Message Size Lookup (MSVC-compatible)
// =============================================================================

/**
 * @brief Get message size from type character (O(1) lookup using switch)
 */
ITCH_FORCE_INLINE constexpr std::size_t get_message_size(char type) noexcept {
    switch (type) {
        case 'S': return sizeof(SystemEventMessage);
        case 'R': return sizeof(StockDirectoryMessage);
        case 'H': return sizeof(StockTradingActionMessage);
        case 'Y': return sizeof(RegSHORestrictionMessage);
        case 'L': return sizeof(MarketParticipantPosMessage);
        case 'V': return sizeof(MWCBDeclineLevelMessage);
        case 'W': return sizeof(MWCBStatusMessage);
        case 'K': return sizeof(IPOQuotingPeriodMessage);
        case 'J': return sizeof(LULDAuctionCollarMessage);
        case 'h': return sizeof(OperationalHaltMessage);
        case 'A': return sizeof(AddOrderMessage);
        case 'F': return sizeof(AddOrderMPIDMessage);
        case 'E': return sizeof(OrderExecutedMessage);
        case 'C': return sizeof(OrderExecutedPriceMessage);
        case 'X': return sizeof(OrderCancelMessage);
        case 'D': return sizeof(OrderDeleteMessage);
        case 'U': return sizeof(OrderReplaceMessage);
        case 'P': return sizeof(TradeMessage);
        case 'Q': return sizeof(CrossTradeMessage);
        case 'B': return sizeof(BrokenTradeMessage);
        case 'I': return sizeof(NOIIMessage);
        case 'N': return sizeof(RPIIMessage);
        default:  return 0;
    }
}

} // namespace itch

