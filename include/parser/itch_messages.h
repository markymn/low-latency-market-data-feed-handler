#pragma once
#include <cstdint>
#include <cstring>

namespace itch {

    // Byte order conversion for big-endian ITCH protocol

    inline uint16_t ntoh16(uint16_t val) { return __builtin_bswap16(val); }
    inline uint32_t ntoh32(uint32_t val) { return __builtin_bswap32(val); }
    inline uint64_t ntoh64(uint64_t val) { return __builtin_bswap64(val); }

    // ITCH Message Types

    enum class MessageType : uint8_t {
        SYSTEM_EVENT = 'S',
        STOCK_DIRECTORY = 'R',
        STOCK_TRADING_ACTION = 'H',
        REG_SH0_RESTRICTION = 'Y',
        MARKET_PARTICIPANT_POSITION = 'L',
        MWCB_STATUS = 'W',
        IPO_QUOTING_PERIOD = 'K',
        LULD_AUCTION_COLLAR = 'J',
        OPERATIONAL_HALT = 'h',
        ADD_ORDER = 'A',
        ADD_ORDER_MPID = 'F',
        ORDER_EXECUTED = 'E',
        ORDER_EXECUTED_WITH_PRICE = 'C',
        ORDER_CANCEL = 'X',
        ORDER_DELETE = 'D',
        ORDER_REPLACE = 'U',
        TRADE = 'P',
        CROSS_TRADE = 'Q',
        BROKER_INDEX = 'B',
        NOII = 'I'
    };

    // ITCH Message Structures

    struct SystemEventMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint8_t  event_code;
    } __attribute__((packed));

    struct StockDirectoryMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char     stock[8];
        uint8_t  market_category;
        uint8_t  financial_status;
        uint32_t round_lot_size;
        uint8_t  round_lots_only;
        uint8_t  issue_classification;
        uint8_t  issue_subtype[2];
        uint8_t  authenticity;
        uint8_t  short_sale_threshold;
        uint8_t  ipo_flag;
        uint8_t  luld_ref_price_tier;
        uint8_t  etp_flag;
        uint32_t etp_leverage_factor;
        uint8_t  inverse_indicator;
    } __attribute__((packed));

    struct StockTradingActionMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char     stock[8];
        uint8_t  trading_state;
        uint8_t  reserved;
        char     reason[4];
    } __attribute__((packed));

    struct RegShoRestrictionMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char     stock[8];
        uint8_t  reg_sho_action;
    } __attribute__((packed));

    struct MarketParticipantPositionMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char     mpid[4];
        char     stock[8];
        uint8_t  primary_market_maker;
        uint8_t  market_maker_mode;
        uint8_t  market_participant_state;
    } __attribute__((packed));

    struct MwcbDeclineLevelMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t level1;
        uint64_t level2;
        uint64_t level3;
    } __attribute__((packed));

    struct MwcbStatusMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint8_t  breached_level;
    } __attribute__((packed));

    struct IpoQuotingPeriodMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char     stock[8];
        uint32_t ipo_quotation_release_time;
        uint8_t  ipo_quotation_release_qualifier;
        uint32_t ipo_price;
    } __attribute__((packed));

    struct LuldAuctionCollarMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char     stock[8];
        uint32_t auction_collar_ref_price;
        uint32_t upper_auction_collar_price;
        uint32_t lower_auction_collar_price;
        uint32_t auction_collar_extension;
    } __attribute__((packed));

    struct OperationalHaltMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char     stock[8];
        uint8_t  market_code;
        uint8_t  operational_halt_action;
    } __attribute__((packed));

    struct AddOrderMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_reference;
        uint8_t  buy_sell;
        uint32_t shares;
        char     stock[8];
        uint32_t price;
    } __attribute__((packed));

    struct AddOrderMpidMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_reference;
        uint8_t  buy_sell;
        uint32_t shares;
        char     stock[8];
        uint32_t price;
        char     attribution[4];
    } __attribute__((packed));

    struct OrderExecutedMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_reference;
        uint32_t executed_shares;
        uint64_t match_number;
    } __attribute__((packed));

    struct OrderExecutedWithPriceMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_reference;
        uint32_t executed_shares;
        uint64_t match_number;
        uint8_t  printable;
        uint32_t execution_price;
    } __attribute__((packed));

    struct OrderCancelMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_reference;
        uint32_t cancelled_shares;
    } __attribute__((packed));

    struct OrderDeleteMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_reference;
    } __attribute__((packed));

    struct OrderReplaceMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t original_order_reference;
        uint64_t new_order_reference;
        uint32_t shares;
        uint32_t price;
    } __attribute__((packed));

    struct TradeMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_reference;
        uint8_t  buy_sell;
        uint32_t shares;
        char     stock[8];
        uint32_t price;
        uint64_t match_number;
    } __attribute__((packed));

    struct CrossTradeMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t shares;
        char     stock[8];
        uint32_t cross_price;
        uint64_t match_number;
        uint8_t  cross_type;
    } __attribute__((packed));

    struct BrokenTradeMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t match_number;
    } __attribute__((packed));

    struct NoiiMessage {
        uint8_t  message_type;
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t paired_shares;
        uint64_t imbalance_shares;
        uint8_t  imbalance_direction;
        char     stock[8];
        uint32_t far_price;
        uint32_t near_price;
        uint32_t current_ref_price;
        uint8_t  cross_type;
        uint8_t  price_variation_indicator;
    } __attribute__((packed));
} // namespace itch