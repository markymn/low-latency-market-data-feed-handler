#pragma once
#include "parser/itch_messages.h"
#include <fstream>
#include <functional>
#include <string>
#include <cstddef>

namespace itch {

    class ITCHReader {
        public:
            explicit ITCHReader(const std::string& filename);
            ~ITCHReader();
            
            // Delete copy constructor and assignment
            ITCHReader(const ITCHReader&) = delete;
            ITCHReader& operator=(const ITCHReader&) = delete;
            
            void read_all();
            
            // Callbacks for different message types
            std::function<void(const SystemEventMessage&)> on_system_event;
            std::function<void(const StockDirectoryMessage&)> on_stock_directory;
            std::function<void(const StockTradingActionMessage&)> on_stock_trading_action;
            std::function<void(const RegShoRestrictionMessage&)> on_reg_sho_restriction;
            std::function<void(const MarketParticipantPositionMessage&)> on_market_participant_position;
            std::function<void(const MwcbDeclineLevelMessage&)> on_mwcb_decline_level;
            std::function<void(const MwcbStatusMessage&)> on_mwcb_status;
            std::function<void(const IpoQuotingPeriodMessage&)> on_ipo_quoting_period;
            std::function<void(const LuldAuctionCollarMessage&)> on_luld_auction_collar;
            std::function<void(const OperationalHaltMessage&)> on_operational_halt;
            std::function<void(const AddOrderMessage&)> on_add_order;
            std::function<void(const AddOrderMpidMessage&)> on_add_order_mpid;
            std::function<void(const OrderExecutedMessage&)> on_order_executed;
            std::function<void(const OrderExecutedWithPriceMessage&)> on_order_executed_with_price;
            std::function<void(const OrderCancelMessage&)> on_order_cancel;
            std::function<void(const OrderDeleteMessage&)> on_order_delete;
            std::function<void(const OrderReplaceMessage&)> on_order_replace;
            std::function<void(const TradeMessage&)> on_trade;
            std::function<void(const CrossTradeMessage&)> on_cross_trade;
            std::function<void(const BrokenTradeMessage&)> on_broken_trade;
            std::function<void(const NoiiMessage&)> on_noii;
            
            // Statistics
            size_t messages_read() const { return messages_read_; }
            size_t bytes_read() const { return bytes_read_; }

        private:
            std::ifstream file_;
            size_t messages_read_;
            size_t bytes_read_;
            
            size_t get_message_size(uint8_t message_type) const;
            void dispatch_message(const uint8_t* buffer, size_t size);
    };
} // namespace itch