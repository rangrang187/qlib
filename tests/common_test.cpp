#include <qlib/common.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace {

using qlib::book;
using qlib::cancel;
using qlib::level;
using qlib::metrics;
using qlib::order;
using qlib::price_level;
using qlib::trade;

// COM-01：数值和声明类型分别固定，互异标签不足以证明数值正确。
static_assert(qlib::price_scale == 10'000'000'000);
static_assert(qlib::qty_scale == 100'000'000);
static_assert(qlib::snapshot_depth == 1);
static_assert(qlib::order_tag == 0);
static_assert(qlib::trade_tag == 1);
static_assert(qlib::level_tag == 2);
static_assert(qlib::cancel_tag == 3);
static_assert(std::is_same_v<decltype(qlib::price_scale), const std::int64_t>);
static_assert(std::is_same_v<decltype(qlib::qty_scale), const std::int64_t>);
static_assert(std::is_same_v<decltype(qlib::snapshot_depth), const std::uint8_t>);
static_assert(std::is_same_v<decltype(qlib::order_tag), const std::uint8_t>);
static_assert(std::is_same_v<decltype(qlib::trade_tag), const std::uint8_t>);
static_assert(std::is_same_v<decltype(qlib::level_tag), const std::uint8_t>);
static_assert(std::is_same_v<decltype(qlib::cancel_tag), const std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<qlib::side>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<qlib::order_type>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<qlib::order_action>, std::uint8_t>);
static_assert(static_cast<std::uint8_t>(qlib::side::buy) == 0);
static_assert(static_cast<std::uint8_t>(qlib::side::sell) == 1);
static_assert(static_cast<std::uint8_t>(qlib::order_type::limit) == 0);
static_assert(static_cast<std::uint8_t>(qlib::order_type::market) == 1);
static_assert(static_cast<std::uint8_t>(qlib::order_action::add) == 0);
static_assert(static_cast<std::uint8_t>(qlib::order_action::modify) == 1);

// COM-04：字段类型包含数组元素及长度；布局只约束顺序，不固定填充和总大小。
static_assert(std::is_trivially_copyable_v<order> && std::is_standard_layout_v<order>);
static_assert(std::is_trivially_copyable_v<cancel> && std::is_standard_layout_v<cancel>);
static_assert(std::is_trivially_copyable_v<trade> && std::is_standard_layout_v<trade>);
static_assert(std::is_trivially_copyable_v<level> && std::is_standard_layout_v<level>);
static_assert(std::is_trivially_copyable_v<book> && std::is_standard_layout_v<book>);
static_assert(std::is_trivially_copyable_v<metrics> && std::is_standard_layout_v<metrics>);

static_assert(std::is_same_v<decltype(order::seq), std::int64_t>);
static_assert(std::is_same_v<decltype(order::order_id), std::int64_t>);
static_assert(std::is_same_v<decltype(order::price), std::int64_t>);
static_assert(std::is_same_v<decltype(order::qty), std::int64_t>);
static_assert(std::is_same_v<decltype(order::new_qty), std::int64_t>);
static_assert(std::is_same_v<decltype(order::prev), order*>);
static_assert(std::is_same_v<decltype(order::next), order*>);
static_assert(std::is_same_v<decltype(order::instrument_id), std::uint32_t>);
static_assert(std::is_same_v<decltype(order::side), qlib::side>);
static_assert(std::is_same_v<decltype(order::type), qlib::order_type>);
static_assert(std::is_same_v<decltype(order::action), qlib::order_action>);
static_assert(std::is_same_v<decltype(order::event_ns), std::int64_t>);
static_assert(std::is_same_v<decltype(order::recv_ns), std::int64_t>);

static_assert(std::is_same_v<decltype(cancel::seq), std::int64_t>);
static_assert(std::is_same_v<decltype(cancel::order_id), std::int64_t>);
static_assert(std::is_same_v<decltype(cancel::qty), std::int64_t>);
static_assert(std::is_same_v<decltype(cancel::instrument_id), std::uint32_t>);
static_assert(std::is_same_v<decltype(cancel::side), qlib::side>);
static_assert(std::is_same_v<decltype(cancel::event_ns), std::int64_t>);
static_assert(std::is_same_v<decltype(cancel::recv_ns), std::int64_t>);

static_assert(std::is_same_v<decltype(trade::seq), std::int64_t>);
static_assert(std::is_same_v<decltype(trade::buy_order_id), std::int64_t>);
static_assert(std::is_same_v<decltype(trade::sell_order_id), std::int64_t>);
static_assert(std::is_same_v<decltype(trade::price), std::int64_t>);
static_assert(std::is_same_v<decltype(trade::qty), std::int64_t>);
static_assert(std::is_same_v<decltype(trade::event_ns), std::int64_t>);
static_assert(std::is_same_v<decltype(trade::instrument_id), std::uint32_t>);
static_assert(std::is_same_v<decltype(trade::side), qlib::side>);
static_assert(std::is_same_v<decltype(trade::recv_ns), std::int64_t>);

static_assert(std::is_same_v<decltype(level::seq), std::int64_t>);
static_assert(std::is_same_v<decltype(level::price), std::int64_t>);
static_assert(std::is_same_v<decltype(level::qty), std::int64_t>);
static_assert(std::is_same_v<decltype(level::event_ns), std::int64_t>);
static_assert(std::is_same_v<decltype(level::instrument_id), std::uint32_t>);
static_assert(std::is_same_v<decltype(level::side), qlib::side>);
static_assert(std::is_same_v<decltype(level::recv_ns), std::int64_t>);

static_assert(std::is_same_v<decltype(book::event_ns), std::int64_t>);
static_assert(std::is_same_v<decltype(book::bid_price), std::int64_t[1]>);
static_assert(std::is_same_v<decltype(book::bid_qty), std::int64_t[1]>);
static_assert(std::is_same_v<decltype(book::ask_price), std::int64_t[1]>);
static_assert(std::is_same_v<decltype(book::ask_qty), std::int64_t[1]>);
static_assert(std::is_same_v<decltype(book::instrument_id), std::uint32_t>);
static_assert(std::is_same_v<decltype(book::recv_ns), std::int64_t>);

static_assert(std::is_same_v<decltype(metrics::ts_ns), std::int64_t>);
static_assert(std::is_same_v<decltype(metrics::feed_messages), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::feed_bytes), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::feed_orders), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::feed_trades), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::feed_levels), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::feed_dropped), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::book_events), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::book_apply_ns), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::book_samples), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::book_instruments), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::book_resting_orders), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::book_memory_bytes), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::writer_orders), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::writer_trades), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::writer_levels), std::uint64_t>);
static_assert(std::is_same_v<decltype(metrics::writer_books), std::uint64_t>);

static_assert(std::is_same_v<decltype(price_level::qty), std::int64_t>);
static_assert(std::is_same_v<decltype(price_level::head), order*>);
static_assert(std::is_same_v<decltype(price_level::tail), order*>);

template <std::size_t N>
constexpr bool strictly_increasing(const std::array<std::size_t, N>& offsets) {
  for (std::size_t i = 1; i < N; ++i) {
    if (offsets[i - 1] >= offsets[i]) {
      return false;
    }
  }
  return true;
}

static_assert(strictly_increasing(std::array{
    offsetof(order, seq), offsetof(order, order_id), offsetof(order, price),
    offsetof(order, qty), offsetof(order, new_qty), offsetof(order, prev),
    offsetof(order, next), offsetof(order, instrument_id), offsetof(order, side),
    offsetof(order, type), offsetof(order, action), offsetof(order, event_ns),
    offsetof(order, recv_ns)}));
static_assert(strictly_increasing(std::array{
    offsetof(cancel, seq), offsetof(cancel, order_id), offsetof(cancel, qty),
    offsetof(cancel, instrument_id), offsetof(cancel, side), offsetof(cancel, event_ns),
    offsetof(cancel, recv_ns)}));
static_assert(strictly_increasing(std::array{
    offsetof(trade, seq), offsetof(trade, buy_order_id), offsetof(trade, sell_order_id),
    offsetof(trade, price), offsetof(trade, qty), offsetof(trade, event_ns),
    offsetof(trade, instrument_id), offsetof(trade, side), offsetof(trade, recv_ns)}));
static_assert(strictly_increasing(std::array{
    offsetof(level, seq), offsetof(level, price), offsetof(level, qty),
    offsetof(level, event_ns), offsetof(level, instrument_id), offsetof(level, side),
    offsetof(level, recv_ns)}));
static_assert(strictly_increasing(std::array{
    offsetof(book, event_ns), offsetof(book, bid_price), offsetof(book, bid_qty),
    offsetof(book, ask_price), offsetof(book, ask_qty), offsetof(book, instrument_id),
    offsetof(book, recv_ns)}));
static_assert(strictly_increasing(std::array{
    offsetof(metrics, ts_ns), offsetof(metrics, feed_messages), offsetof(metrics, feed_bytes),
    offsetof(metrics, feed_orders), offsetof(metrics, feed_trades), offsetof(metrics, feed_levels),
    offsetof(metrics, feed_dropped), offsetof(metrics, book_events),
    offsetof(metrics, book_apply_ns),
    offsetof(metrics, book_samples), offsetof(metrics, book_instruments),
    offsetof(metrics, book_resting_orders), offsetof(metrics, book_memory_bytes),
    offsetof(metrics, writer_orders), offsetof(metrics, writer_trades),
    offsetof(metrics, writer_levels), offsetof(metrics, writer_books)}));
static_assert(strictly_increasing(std::array{
    offsetof(price_level, qty), offsetof(price_level, head), offsetof(price_level, tail)}));

// COM-05/08：比较完整类型可同时发现成员增减、替换和重排。
static_assert(std::is_same_v<qlib::feed_event, std::variant<order, trade, level, cancel>>);
static_assert(std::is_same_v<qlib::record, std::variant<order, trade, level, cancel, book>>);
static_assert(!std::is_constructible_v<qlib::feed_event, book>);
static_assert(!std::is_constructible_v<qlib::feed_event, metrics>);
static_assert(!std::is_constructible_v<qlib::feed_event, price_level>);
static_assert(!std::is_constructible_v<qlib::record, metrics>);
static_assert(!std::is_constructible_v<qlib::record, price_level>);

TEST(Common, DefaultCacheLineAndFixedPointUnits) {
  EXPECT_EQ(QLIB_CACHE_LINE, 64);
  EXPECT_EQ(125 * qlib::price_scale / 100, 12'500'000'000);
  EXPECT_EQ(25 * qlib::qty_scale / 10, 250'000'000);
}

// COM-02/03：原值和复制值都与手写预期比较，复制对象不充当预期来源。
TEST(Common, OrderFieldsAndCopy) {
  order previous{};
  order next{};
  order value{};
  value.seq = 101;
  value.order_id = 102;
  value.price = 125 * qlib::price_scale / 100;
  value.qty = 3 * qlib::qty_scale;
  value.new_qty = 2 * qlib::qty_scale;
  value.prev = &previous;
  value.next = &next;
  value.instrument_id = 103;
  value.side = qlib::side::sell;
  value.type = qlib::order_type::market;
  value.action = qlib::order_action::modify;
  value.event_ns = 1'700'000'000'000'000'104;
  value.recv_ns = 1'700'000'000'000'000'105;
  const order copy = value;

  for (const order* actual : std::array<const order*, 2>{&value, &copy}) {
    SCOPED_TRACE(actual == &value ? "original" : "copy");
    EXPECT_EQ(actual->seq, 101);
    EXPECT_EQ(actual->order_id, 102);
    EXPECT_EQ(actual->price, 12'500'000'000);
    EXPECT_EQ(actual->qty, 300'000'000);
    EXPECT_EQ(actual->new_qty, 200'000'000);
    EXPECT_EQ(actual->prev, &previous);
    EXPECT_EQ(actual->next, &next);
    EXPECT_EQ(actual->instrument_id, 103U);
    EXPECT_EQ(actual->side, qlib::side::sell);
    EXPECT_EQ(actual->type, qlib::order_type::market);
    EXPECT_EQ(actual->action, qlib::order_action::modify);
    EXPECT_EQ(actual->event_ns, 1'700'000'000'000'000'104);
    EXPECT_EQ(actual->recv_ns, 1'700'000'000'000'000'105);
  }
}

TEST(Common, CancelFieldsAndCopy) {
  cancel value{};
  value.seq = 201;
  value.order_id = 202;
  value.qty = 4 * qlib::qty_scale;
  value.instrument_id = 203;
  value.side = qlib::side::buy;
  value.event_ns = 1'700'000'000'000'000'204;
  value.recv_ns = 1'700'000'000'000'000'205;
  const cancel copy = value;

  for (const cancel* actual : std::array<const cancel*, 2>{&value, &copy}) {
    SCOPED_TRACE(actual == &value ? "original" : "copy");
    EXPECT_EQ(actual->seq, 201);
    EXPECT_EQ(actual->order_id, 202);
    EXPECT_EQ(actual->qty, 400'000'000);
    EXPECT_EQ(actual->instrument_id, 203U);
    EXPECT_EQ(actual->side, qlib::side::buy);
    EXPECT_EQ(actual->event_ns, 1'700'000'000'000'000'204);
    EXPECT_EQ(actual->recv_ns, 1'700'000'000'000'000'205);
  }
}

TEST(Common, TradeFieldsAndCopy) {
  trade value{};
  value.seq = 301;
  value.buy_order_id = 302;
  value.sell_order_id = 303;
  value.price = 135 * qlib::price_scale / 100;
  value.qty = 5 * qlib::qty_scale;
  value.event_ns = 1'700'000'000'000'000'304;
  value.instrument_id = 305;
  value.side = qlib::side::sell;
  value.recv_ns = 1'700'000'000'000'000'306;
  const trade copy = value;

  for (const trade* actual : std::array<const trade*, 2>{&value, &copy}) {
    SCOPED_TRACE(actual == &value ? "original" : "copy");
    EXPECT_EQ(actual->seq, 301);
    EXPECT_EQ(actual->buy_order_id, 302);
    EXPECT_EQ(actual->sell_order_id, 303);
    EXPECT_EQ(actual->price, 13'500'000'000);
    EXPECT_EQ(actual->qty, 500'000'000);
    EXPECT_EQ(actual->event_ns, 1'700'000'000'000'000'304);
    EXPECT_EQ(actual->instrument_id, 305U);
    EXPECT_EQ(actual->side, qlib::side::sell);
    EXPECT_EQ(actual->recv_ns, 1'700'000'000'000'000'306);
  }
}

TEST(Common, LevelFieldsAndCopy) {
  level value{};
  value.seq = 401;
  value.price = 145 * qlib::price_scale / 100;
  value.qty = 6 * qlib::qty_scale;
  value.event_ns = 1'700'000'000'000'000'402;
  value.instrument_id = 403;
  value.side = qlib::side::buy;
  value.recv_ns = 1'700'000'000'000'000'404;
  const level copy = value;

  for (const level* actual : std::array<const level*, 2>{&value, &copy}) {
    SCOPED_TRACE(actual == &value ? "original" : "copy");
    EXPECT_EQ(actual->seq, 401);
    EXPECT_EQ(actual->price, 14'500'000'000);
    EXPECT_EQ(actual->qty, 600'000'000);
    EXPECT_EQ(actual->event_ns, 1'700'000'000'000'000'402);
    EXPECT_EQ(actual->instrument_id, 403U);
    EXPECT_EQ(actual->side, qlib::side::buy);
    EXPECT_EQ(actual->recv_ns, 1'700'000'000'000'000'404);
  }

  value.qty = 0;
  EXPECT_EQ(value.qty, 0);
  value.qty = -100'000'000;
  EXPECT_EQ(value.qty, -100'000'000);
}

TEST(Common, BookFieldsAndCopy) {
  book value{};
  value.event_ns = 1'700'000'000'000'000'501;
  value.bid_price[0] = 155 * qlib::price_scale / 100;
  value.bid_qty[0] = 7 * qlib::qty_scale;
  value.ask_price[0] = 165 * qlib::price_scale / 100;
  value.ask_qty[0] = 8 * qlib::qty_scale;
  value.instrument_id = 502;
  value.recv_ns = 1'700'000'000'000'000'503;
  const book copy = value;

  for (const book* actual : std::array<const book*, 2>{&value, &copy}) {
    SCOPED_TRACE(actual == &value ? "original" : "copy");
    EXPECT_EQ(actual->event_ns, 1'700'000'000'000'000'501);
    EXPECT_EQ(actual->bid_price[0], 15'500'000'000);
    EXPECT_EQ(actual->bid_qty[0], 700'000'000);
    EXPECT_EQ(actual->ask_price[0], 16'500'000'000);
    EXPECT_EQ(actual->ask_qty[0], 800'000'000);
    EXPECT_EQ(actual->instrument_id, 502U);
    EXPECT_EQ(actual->recv_ns, 1'700'000'000'000'000'503);
  }

  value.bid_qty[0] = 900'000'000;
  EXPECT_EQ(copy.bid_qty[0], 700'000'000);
}

TEST(Common, MetricsValueInitializationZerosEveryField) {
  const metrics value{};
  EXPECT_EQ(value.ts_ns, 0);
  EXPECT_EQ(value.feed_messages, 0U);
  EXPECT_EQ(value.feed_bytes, 0U);
  EXPECT_EQ(value.feed_orders, 0U);
  EXPECT_EQ(value.feed_trades, 0U);
  EXPECT_EQ(value.feed_levels, 0U);
  EXPECT_EQ(value.feed_dropped, 0U);
  EXPECT_EQ(value.book_events, 0U);
  EXPECT_EQ(value.book_apply_ns, 0U);
  EXPECT_EQ(value.book_samples, 0U);
  EXPECT_EQ(value.book_instruments, 0U);
  EXPECT_EQ(value.book_resting_orders, 0U);
  EXPECT_EQ(value.book_memory_bytes, 0U);
  EXPECT_EQ(value.writer_orders, 0U);
  EXPECT_EQ(value.writer_trades, 0U);
  EXPECT_EQ(value.writer_levels, 0U);
  EXPECT_EQ(value.writer_books, 0U);
}

TEST(Common, MetricsFieldsAndCopy) {
  metrics value{};
  value.ts_ns = 1'700'000'000'000'000'601;
  value.feed_messages = 1001;
  value.feed_bytes = 64'002;
  value.feed_orders = 603;
  value.feed_trades = 204;
  value.feed_levels = 105;
  value.feed_dropped = 6;
  value.book_events = 907;
  value.book_apply_ns = 45'008;
  value.book_samples = 109;
  value.book_instruments = 10;
  value.book_resting_orders = 211;
  value.book_memory_bytes = 32'012;
  value.writer_orders = 513;
  value.writer_trades = 114;
  value.writer_levels = 85;
  value.writer_books = 16;
  const metrics copy = value;

  for (const metrics* actual : std::array<const metrics*, 2>{&value, &copy}) {
    SCOPED_TRACE(actual == &value ? "original" : "copy");
    EXPECT_EQ(actual->ts_ns, 1'700'000'000'000'000'601);
    EXPECT_EQ(actual->feed_messages, 1001U);
    EXPECT_EQ(actual->feed_bytes, 64'002U);
    EXPECT_EQ(actual->feed_orders, 603U);
    EXPECT_EQ(actual->feed_trades, 204U);
    EXPECT_EQ(actual->feed_levels, 105U);
    EXPECT_EQ(actual->feed_dropped, 6U);
    EXPECT_EQ(actual->book_events, 907U);
    EXPECT_EQ(actual->book_apply_ns, 45'008U);
    EXPECT_EQ(actual->book_samples, 109U);
    EXPECT_EQ(actual->book_instruments, 10U);
    EXPECT_EQ(actual->book_resting_orders, 211U);
    EXPECT_EQ(actual->book_memory_bytes, 32'012U);
    EXPECT_EQ(actual->writer_orders, 513U);
    EXPECT_EQ(actual->writer_trades, 114U);
    EXPECT_EQ(actual->writer_levels, 85U);
    EXPECT_EQ(actual->writer_books, 16U);
  }

  // 当前挂单量允许下降；累计消息数和已有样本由调用方分别维护。
  value.book_resting_orders = 200;
  EXPECT_EQ(value.book_resting_orders, 200U);
  EXPECT_EQ(value.feed_messages, 1001U);
  EXPECT_EQ(copy.book_resting_orders, 211U);
}

template <typename Variant, typename T>
void expect_variant_round_trip(std::int64_t T::* member, std::int64_t expected,
                              std::size_t expected_index) {
  T value{};
  value.*member = expected;
  Variant constructed{value};
  ASSERT_TRUE(std::holds_alternative<T>(constructed));
  EXPECT_EQ(constructed.index(), expected_index);
  EXPECT_EQ(std::get<T>(constructed).*member, expected);

  using other_type = std::conditional_t<std::is_same_v<T, order>, trade, order>;
  Variant assigned{other_type{}};
  assigned = value;
  ASSERT_TRUE(std::holds_alternative<T>(assigned));
  EXPECT_EQ(assigned.index(), expected_index);
  EXPECT_EQ(std::get<T>(assigned).*member, expected);

  value.*member = 0;
  EXPECT_EQ(std::get<T>(constructed).*member, expected);
  EXPECT_EQ(std::get<T>(assigned).*member, expected);
}

TEST(Common, FeedEventConstructsAssignsAndReadsEveryAlternative) {
  expect_variant_round_trip<qlib::feed_event>(&order::seq, 701, 0);
  expect_variant_round_trip<qlib::feed_event>(&trade::seq, 702, 1);
  expect_variant_round_trip<qlib::feed_event>(&level::seq, 703, 2);
  expect_variant_round_trip<qlib::feed_event>(&cancel::seq, 704, 3);
}

TEST(Common, RecordConstructsAssignsAndReadsEveryAlternative) {
  expect_variant_round_trip<qlib::record>(&order::seq, 801, 0);
  expect_variant_round_trip<qlib::record>(&trade::seq, 802, 1);
  expect_variant_round_trip<qlib::record>(&level::seq, 803, 2);
  expect_variant_round_trip<qlib::record>(&cancel::seq, 804, 3);
  expect_variant_round_trip<qlib::record>(&book::event_ns, 805, 4);
}

// COM-06：各分支返回独立标记，泛型兜底与 trade 专用分支使用相同参数限定。
TEST(Common, FeedVisitorSelectsEveryBranch) {
  const auto visitor = qlib::overloaded{
      [](const order&) { return 11; },
      [](const trade&) { return 22; },
      [](const level&) { return 33; },
      [](const cancel&) { return 44; }};
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{order{}}), 11);
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{trade{}}), 22);
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{level{}}), 33);
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{cancel{}}), 44);
}

TEST(Common, RecordVisitorSelectsEveryBranch) {
  const auto visitor = qlib::overloaded{
      [](const order&) { return 51; },
      [](const trade&) { return 62; },
      [](const level&) { return 73; },
      [](const cancel&) { return 84; },
      [](const book&) { return 95; }};
  EXPECT_EQ(std::visit(visitor, qlib::record{order{}}), 51);
  EXPECT_EQ(std::visit(visitor, qlib::record{trade{}}), 62);
  EXPECT_EQ(std::visit(visitor, qlib::record{level{}}), 73);
  EXPECT_EQ(std::visit(visitor, qlib::record{cancel{}}), 84);
  EXPECT_EQ(std::visit(visitor, qlib::record{book{}}), 95);
}

TEST(Common, TradeOverloadTakesPrecedenceOverGenericFallback) {
  const auto visitor = qlib::overloaded{
      [](const trade&) { return 101; },
      [](const auto&) { return 202; }};
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{order{}}), 202);
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{trade{}}), 101);
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{level{}}), 202);
  EXPECT_EQ(std::visit(visitor, qlib::feed_event{cancel{}}), 202);
  EXPECT_EQ(std::visit(visitor, qlib::record{order{}}), 202);
  EXPECT_EQ(std::visit(visitor, qlib::record{trade{}}), 101);
  EXPECT_EQ(std::visit(visitor, qlib::record{level{}}), 202);
  EXPECT_EQ(std::visit(visitor, qlib::record{cancel{}}), 202);
  EXPECT_EQ(std::visit(visitor, qlib::record{book{}}), 202);
}

TEST(Common, WrongAlternativeAccessThrowsBadVariantAccess) {
  const qlib::feed_event event{order{}};
  ASSERT_TRUE(std::holds_alternative<order>(event));
  EXPECT_THROW(static_cast<void>(std::get<trade>(event)), std::bad_variant_access);

  const qlib::record stored{book{}};
  ASSERT_TRUE(std::holds_alternative<book>(stored));
  EXPECT_THROW(static_cast<void>(std::get<cancel>(stored)), std::bad_variant_access);
}

// COM-07：普通声明的默认状态与借用订单的存活范围分别验证。
TEST(PriceLevel, DefaultConstructionIsEmpty) {
  price_level value;
  EXPECT_EQ(value.qty, 0);
  EXPECT_EQ(value.head, nullptr);
  EXPECT_EQ(value.tail, nullptr);
}

TEST(PriceLevel, AggregateQuantityDoesNotRequireOrderLinks) {
  price_level value;
  value.qty = 8 * qlib::qty_scale;
  EXPECT_EQ(value.qty, 800'000'000);
  EXPECT_EQ(value.head, nullptr);
  EXPECT_EQ(value.tail, nullptr);
}

TEST(PriceLevel, CallerMaintainsLinksAndAggregateQuantity) {
  order first{};
  order second{};
  first.order_id = 901;
  first.price = 175 * qlib::price_scale / 100;
  first.qty = 2 * qlib::qty_scale;
  first.side = qlib::side::buy;
  second.order_id = 902;
  second.price = 175 * qlib::price_scale / 100;
  second.qty = 3 * qlib::qty_scale;
  second.side = qlib::side::buy;

  {
    price_level value;
    first.next = &second;
    second.prev = &first;
    value.head = &first;
    value.tail = &second;
    value.qty = first.qty + second.qty;

    EXPECT_EQ(value.head, &first);
    EXPECT_EQ(value.tail, &second);
    EXPECT_EQ(value.qty, 500'000'000);
    EXPECT_EQ(first.prev, nullptr);
    EXPECT_EQ(first.next, &second);
    EXPECT_EQ(second.prev, &first);
    EXPECT_EQ(second.next, nullptr);

    const price_level copy = value;
    EXPECT_EQ(copy.head, &first);
    EXPECT_EQ(copy.tail, &second);
    EXPECT_EQ(copy.qty, 500'000'000);

    first.qty = 4 * qlib::qty_scale;
    EXPECT_EQ(value.qty, 500'000'000);
    value.qty = first.qty + second.qty;
    EXPECT_EQ(value.qty, 700'000'000);
    EXPECT_EQ(copy.qty, 500'000'000);
  }

  // 价位销毁后，调用方仍拥有两个订单及其链接。
  EXPECT_EQ(first.order_id, 901);
  EXPECT_EQ(second.order_id, 902);
  EXPECT_EQ(first.qty, 400'000'000);
  EXPECT_EQ(second.qty, 300'000'000);
  EXPECT_EQ(first.next, &second);
  EXPECT_EQ(second.prev, &first);
  first.next = nullptr;
  second.prev = nullptr;
}

}  // namespace
