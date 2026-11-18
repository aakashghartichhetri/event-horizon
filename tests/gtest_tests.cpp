#include "event_horizon/exchange.hpp"
#include "event_horizon/replay.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace eh = event_horizon;

TEST(Exchange, TradesSettleAtRestingPrice) {
    eh::Exchange exchange;
    ASSERT_TRUE(exchange.add_account("Buyer", 10000, 0).ok);
    ASSERT_TRUE(exchange.add_account("Seller", 0, 1).ok);
    ASSERT_TRUE(exchange.place_order("ask", "Seller", eh::Side::sell, 1, 2500).ok);
    ASSERT_TRUE(exchange.place_order("bid", "Buyer", eh::Side::buy, 1, 3000).ok);
    EXPECT_EQ(exchange.clearing_cash_cents(), 2500);
    EXPECT_EQ(exchange.settle_all().settled, 1U);
    EXPECT_NO_THROW(exchange.verify_invariants());
    EXPECT_EQ(exchange.accounts()[0].cash_cents, 7500);
    EXPECT_EQ(exchange.accounts()[0].shares, 1);
    EXPECT_EQ(exchange.accounts()[1].cash_cents, 2500);
}

TEST(Replay, OutageBlocksSettlementUntilRecovered) {
    eh::Exchange exchange;
    std::istringstream events(
        "ACCOUNT,A,5000,0\nACCOUNT,B,0,1\n"
        "PLACE,s,B,SELL,1,4000\nPLACE,b,A,BUY,1,4000\n"
        "PAYMENTS_DOWN\nSETTLE\nPAYMENTS_UP\nSETTLE\n");
    std::ostringstream log;
    ASSERT_NO_THROW(eh::replay_events(events, exchange, log));
    EXPECT_EQ(exchange.metrics().trades_executed, 1);
    EXPECT_EQ(exchange.metrics().trades_settled, 1);
    EXPECT_EQ(exchange.metrics().blocked_settlements, 1);
    EXPECT_EQ(exchange.pending_trade_count(), 0U);
    EXPECT_NO_THROW(exchange.verify_invariants());
}

TEST(Exchange, ReservedBalancesPreventDoubleSpending) {
    eh::Exchange exchange;
    ASSERT_TRUE(exchange.add_account("Buyer", 1000, 0).ok);
    EXPECT_TRUE(exchange.place_order("first", "Buyer", eh::Side::buy, 1, 800).ok);
    EXPECT_FALSE(exchange.place_order("second", "Buyer", eh::Side::buy, 1, 300).ok);
    EXPECT_TRUE(exchange.cancel_order("first", "Buyer").ok);
    EXPECT_TRUE(exchange.place_order("third", "Buyer", eh::Side::buy, 1, 300).ok);
    EXPECT_NO_THROW(exchange.verify_invariants());
}
