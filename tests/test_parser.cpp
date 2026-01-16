/**
 * @file test_parser.cpp
 * @brief Unit tests for ITCH 5.0 Parser
 */

#include "../include/itch_parser.hpp"
#include <cassert>
#include <iostream>
#include <cstring>

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

// Helper to set big-endian values
void set_be16(std::uint16_t& field, std::uint16_t value) {
    field = endian::be16_to_host(value);
}

void set_be32(std::uint32_t& field, std::uint32_t value) {
    field = endian::be32_to_host(value);
}

void set_be64(std::uint64_t& field, std::uint64_t value) {
    field = endian::be64_to_host(value);
}

void set_timestamp(std::uint8_t* ts, Timestamp value) {
    ts[0] = static_cast<std::uint8_t>((value >> 40) & 0xFF);
    ts[1] = static_cast<std::uint8_t>((value >> 32) & 0xFF);
    ts[2] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    ts[3] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    ts[4] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    ts[5] = static_cast<std::uint8_t>(value & 0xFF);
}

// =============================================================================
// Message Size Tests
// =============================================================================

TEST(message_sizes) {
    // Verify all message sizes match ITCH 5.0 specification
    assert(sizeof(SystemEventMessage) == 12);
    assert(sizeof(StockDirectoryMessage) == 39);
    assert(sizeof(StockTradingActionMessage) == 25);
    assert(sizeof(RegSHORestrictionMessage) == 20);
    assert(sizeof(MarketParticipantPosMessage) == 26);
    assert(sizeof(MWCBDeclineLevelMessage) == 35);
    assert(sizeof(MWCBStatusMessage) == 12);
    assert(sizeof(IPOQuotingPeriodMessage) == 28);
    assert(sizeof(LULDAuctionCollarMessage) == 35);
    assert(sizeof(OperationalHaltMessage) == 21);
    assert(sizeof(AddOrderMessage) == 36);
    assert(sizeof(AddOrderMPIDMessage) == 40);
    assert(sizeof(OrderExecutedMessage) == 31);
    assert(sizeof(OrderExecutedPriceMessage) == 36);
    assert(sizeof(OrderCancelMessage) == 23);
    assert(sizeof(OrderDeleteMessage) == 19);
    assert(sizeof(OrderReplaceMessage) == 35);
    assert(sizeof(TradeMessage) == 44);
    assert(sizeof(CrossTradeMessage) == 40);
    assert(sizeof(BrokenTradeMessage) == 19);
    assert(sizeof(NOIIMessage) == 50);
    assert(sizeof(RPIIMessage) == 20);
}

TEST(message_size_lookup) {
    assert(get_message_size('S') == 12);
    assert(get_message_size('R') == 39);
    assert(get_message_size('A') == 36);
    assert(get_message_size('F') == 40);
    assert(get_message_size('E') == 31);
    assert(get_message_size('C') == 36);
    assert(get_message_size('X') == 23);
    assert(get_message_size('D') == 19);
    assert(get_message_size('U') == 35);
    assert(get_message_size('P') == 44);
    assert(get_message_size('Q') == 40);
    assert(get_message_size('B') == 19);
    assert(get_message_size('I') == 50);
    assert(get_message_size('N') == 20);
    
    // Unknown message types
    assert(get_message_size('Z') == 0);
    assert(get_message_size('\0') == 0);
}

// =============================================================================
// Endianness Conversion Tests
// =============================================================================

TEST(endianness_conversion) {
    // 16-bit
    assert(endian::be16_to_host(0x0102) == 0x0201);
    
    // 32-bit
    assert(endian::be32_to_host(0x01020304) == 0x04030201);
    
    // 64-bit
    assert(endian::be64_to_host(0x0102030405060708ULL) == 0x0807060504030201ULL);
    
    // 48-bit timestamp
    std::uint8_t ts[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    Timestamp result = endian::be48_to_host(ts);
    assert(result == 0x010203040506ULL);
}

// =============================================================================
// Parser Tests
// =============================================================================

class TestHandler : public MessageHandler {
public:
    int add_order_count = 0;
    int order_executed_count = 0;
    int order_cancel_count = 0;
    int order_delete_count = 0;
    int system_event_count = 0;
    int stock_directory_count = 0;
    int trade_count = 0;
    
    OrderId last_order_id = 0;
    Price last_price = 0;
    Quantity last_quantity = 0;
    Side last_side = Side::Buy;
    Timestamp last_timestamp = 0;
    
    void on_add_order(const AddOrderMessage& msg, Timestamp ts) override {
        ++add_order_count;
        last_order_id = endian::be64_to_host(msg.order_ref_number);
        last_price = endian::be32_to_host(msg.price);
        last_quantity = endian::be32_to_host(msg.shares);
        last_side = char_to_side(msg.buy_sell_indicator);
        last_timestamp = ts;
    }
    
    void on_order_executed(const OrderExecutedMessage& msg, Timestamp ts) override {
        ++order_executed_count;
        last_order_id = endian::be64_to_host(msg.order_ref_number);
        last_quantity = endian::be32_to_host(msg.executed_shares);
        last_timestamp = ts;
    }
    
    void on_order_cancel(const OrderCancelMessage& msg, Timestamp ts) override {
        ++order_cancel_count;
        last_order_id = endian::be64_to_host(msg.order_ref_number);
        last_quantity = endian::be32_to_host(msg.cancelled_shares);
        last_timestamp = ts;
    }
    
    void on_order_delete(const OrderDeleteMessage& msg, Timestamp ts) override {
        ++order_delete_count;
        last_order_id = endian::be64_to_host(msg.order_ref_number);
        last_timestamp = ts;
    }
    
    void on_system_event(const SystemEventMessage& msg, Timestamp ts) override {
        ++system_event_count;
        last_timestamp = ts;
        (void)msg;
    }
    
    void on_stock_directory(const StockDirectoryMessage& msg, Timestamp ts) override {
        ++stock_directory_count;
        last_timestamp = ts;
        (void)msg;
    }
    
    void on_trade(const TradeMessage& msg, Timestamp ts) override {
        ++trade_count;
        last_price = endian::be32_to_host(msg.price);
        last_quantity = endian::be32_to_host(msg.shares);
        last_timestamp = ts;
    }
    
    void reset() {
        add_order_count = 0;
        order_executed_count = 0;
        order_cancel_count = 0;
        order_delete_count = 0;
        system_event_count = 0;
        stock_directory_count = 0;
        trade_count = 0;
        last_order_id = 0;
        last_price = 0;
        last_quantity = 0;
        last_side = Side::Buy;
        last_timestamp = 0;
    }
};

TEST(parse_add_order) {
    TestHandler handler;
    ITCHParser parser(&handler);
    
    char buffer[64];
    auto* msg = reinterpret_cast<AddOrderMessage*>(buffer);
    msg->message_type = 'A';
    set_be16(msg->stock_locate, 123);
    set_be16(msg->tracking_number, 0);
    set_timestamp(msg->timestamp, 34200000000000ULL); // 9:30 AM
    set_be64(msg->order_ref_number, 1001);
    msg->buy_sell_indicator = 'B';
    set_be32(msg->shares, 500);
    std::memset(msg->stock, ' ', 8);
    std::memcpy(msg->stock, "AAPL", 4);
    set_be32(msg->price, 1500000); // $150.0000
    
    std::size_t consumed = parser.parse_message(buffer, sizeof(AddOrderMessage));
    
    assert(consumed == sizeof(AddOrderMessage));
    assert(handler.add_order_count == 1);
    assert(handler.last_order_id == 1001);
    assert(handler.last_price == 1500000);
    assert(handler.last_quantity == 500);
    assert(handler.last_side == Side::Buy);
    assert(handler.last_timestamp == 34200000000000ULL);
}

TEST(parse_order_executed) {
    TestHandler handler;
    ITCHParser parser(&handler);
    
    char buffer[64];
    auto* msg = reinterpret_cast<OrderExecutedMessage*>(buffer);
    msg->message_type = 'E';
    set_be16(msg->stock_locate, 123);
    set_be16(msg->tracking_number, 0);
    set_timestamp(msg->timestamp, 34200100000000ULL);
    set_be64(msg->order_ref_number, 1001);
    set_be32(msg->executed_shares, 100);
    set_be64(msg->match_number, 5001);
    
    std::size_t consumed = parser.parse_message(buffer, sizeof(OrderExecutedMessage));
    
    assert(consumed == sizeof(OrderExecutedMessage));
    assert(handler.order_executed_count == 1);
    assert(handler.last_order_id == 1001);
    assert(handler.last_quantity == 100);
}

TEST(parse_order_delete) {
    TestHandler handler;
    ITCHParser parser(&handler);
    
    char buffer[64];
    auto* msg = reinterpret_cast<OrderDeleteMessage*>(buffer);
    msg->message_type = 'D';
    set_be16(msg->stock_locate, 123);
    set_be16(msg->tracking_number, 0);
    set_timestamp(msg->timestamp, 34200200000000ULL);
    set_be64(msg->order_ref_number, 1001);
    
    std::size_t consumed = parser.parse_message(buffer, sizeof(OrderDeleteMessage));
    
    assert(consumed == sizeof(OrderDeleteMessage));
    assert(handler.order_delete_count == 1);
    assert(handler.last_order_id == 1001);
}

TEST(parse_multiple_messages) {
    TestHandler handler;
    ITCHParser parser(&handler);
    
    // Create buffer with multiple messages
    std::vector<char> buffer(1024);
    std::size_t offset = 0;
    
    // Add order 1
    auto* msg1 = reinterpret_cast<AddOrderMessage*>(buffer.data() + offset);
    msg1->message_type = 'A';
    set_be16(msg1->stock_locate, 1);
    set_be16(msg1->tracking_number, 0);
    set_timestamp(msg1->timestamp, 1000);
    set_be64(msg1->order_ref_number, 1);
    msg1->buy_sell_indicator = 'B';
    set_be32(msg1->shares, 100);
    std::memset(msg1->stock, ' ', 8);
    set_be32(msg1->price, 1000000);
    offset += sizeof(AddOrderMessage);
    
    // Add order 2
    auto* msg2 = reinterpret_cast<AddOrderMessage*>(buffer.data() + offset);
    msg2->message_type = 'A';
    set_be16(msg2->stock_locate, 2);
    set_be16(msg2->tracking_number, 0);
    set_timestamp(msg2->timestamp, 2000);
    set_be64(msg2->order_ref_number, 2);
    msg2->buy_sell_indicator = 'S';
    set_be32(msg2->shares, 200);
    std::memset(msg2->stock, ' ', 8);
    set_be32(msg2->price, 1010000);
    offset += sizeof(AddOrderMessage);
    
    // Parse buffer
    std::size_t consumed = parser.parse(buffer.data(), offset);
    
    assert(consumed == offset);
    assert(handler.add_order_count == 2);
    assert(parser.stats().messages_parsed == 2);
    assert(parser.stats().bytes_processed == 2 * sizeof(AddOrderMessage));
}

TEST(parse_insufficient_data) {
    TestHandler handler;
    ITCHParser parser(&handler);
    
    char buffer[16];
    buffer[0] = 'A'; // Add order message (36 bytes) but only 16 bytes provided
    
    std::size_t consumed = parser.parse_message(buffer, 16);
    
    assert(consumed == 0); // Should fail due to insufficient data
    assert(handler.add_order_count == 0);
}

TEST(parse_unknown_message_type) {
    TestHandler handler;
    ITCHParser parser(&handler);
    
    char buffer[64];
    buffer[0] = 'Z'; // Unknown message type
    
    std::size_t consumed = parser.parse_message(buffer, 64);
    
    assert(consumed == 0);
    assert(parser.stats().parse_errors == 1);
}

// =============================================================================
// Template Parser Tests
// =============================================================================

struct StaticHandler {
    static int add_order_count;
    static OrderId last_order_id;
    
    static void on_add_order(const AddOrderMessage& msg, Timestamp) {
        ++add_order_count;
        last_order_id = endian::be64_to_host(msg.order_ref_number);
    }
    
    // Stub implementations for other message types
    static void on_add_order_mpid(const AddOrderMPIDMessage&, Timestamp) {}
    static void on_order_executed(const OrderExecutedMessage&, Timestamp) {}
    static void on_order_executed_price(const OrderExecutedPriceMessage&, Timestamp) {}
    static void on_order_cancel(const OrderCancelMessage&, Timestamp) {}
    static void on_order_delete(const OrderDeleteMessage&, Timestamp) {}
    static void on_order_replace(const OrderReplaceMessage&, Timestamp) {}
    static void on_trade(const TradeMessage&, Timestamp) {}
    static void on_cross_trade(const CrossTradeMessage&, Timestamp) {}
    static void on_broken_trade(const BrokenTradeMessage&, Timestamp) {}
    static void on_system_event(const SystemEventMessage&, Timestamp) {}
    static void on_stock_directory(const StockDirectoryMessage&, Timestamp) {}
    static void on_stock_trading_action(const StockTradingActionMessage&, Timestamp) {}
    static void on_reg_sho_restriction(const RegSHORestrictionMessage&, Timestamp) {}
    static void on_market_participant_pos(const MarketParticipantPosMessage&, Timestamp) {}
    static void on_mwcb_decline_level(const MWCBDeclineLevelMessage&, Timestamp) {}
    static void on_mwcb_status(const MWCBStatusMessage&, Timestamp) {}
    static void on_ipo_quoting_period(const IPOQuotingPeriodMessage&, Timestamp) {}
    static void on_luld_auction_collar(const LULDAuctionCollarMessage&, Timestamp) {}
    static void on_operational_halt(const OperationalHaltMessage&, Timestamp) {}
    static void on_noii(const NOIIMessage&, Timestamp) {}
    static void on_rpii(const RPIIMessage&, Timestamp) {}
    
    static void reset() {
        add_order_count = 0;
        last_order_id = 0;
    }
};

int StaticHandler::add_order_count = 0;
OrderId StaticHandler::last_order_id = 0;

TEST(template_parser) {
    StaticHandler::reset();
    TemplateParser<StaticHandler> parser;
    
    char buffer[64];
    auto* msg = reinterpret_cast<AddOrderMessage*>(buffer);
    msg->message_type = 'A';
    set_be16(msg->stock_locate, 1);
    set_be16(msg->tracking_number, 0);
    set_timestamp(msg->timestamp, 1000);
    set_be64(msg->order_ref_number, 42);
    msg->buy_sell_indicator = 'B';
    set_be32(msg->shares, 100);
    std::memset(msg->stock, ' ', 8);
    set_be32(msg->price, 1000000);
    
    std::size_t consumed = parser.parse_message(buffer, sizeof(AddOrderMessage));
    
    assert(consumed == sizeof(AddOrderMessage));
    assert(StaticHandler::add_order_count == 1);
    assert(StaticHandler::last_order_id == 42);
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "Running ITCH Parser Tests\n";
    std::cout << std::string(40, '=') << "\n";
    
    // Message size tests
    std::cout << "\nMessage Size Tests:\n";
    RUN_TEST(message_sizes);
    RUN_TEST(message_size_lookup);
    
    // Endianness tests
    std::cout << "\nEndianness Conversion Tests:\n";
    RUN_TEST(endianness_conversion);
    
    // Parser tests
    std::cout << "\nParser Tests:\n";
    RUN_TEST(parse_add_order);
    RUN_TEST(parse_order_executed);
    RUN_TEST(parse_order_delete);
    RUN_TEST(parse_multiple_messages);
    RUN_TEST(parse_insufficient_data);
    RUN_TEST(parse_unknown_message_type);
    
    // Template parser tests
    std::cout << "\nTemplate Parser Tests:\n";
    RUN_TEST(template_parser);
    
    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "All parser tests PASSED!\n";
    
    return 0;
}
