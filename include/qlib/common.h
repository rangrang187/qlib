#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

// 同一程序的所有翻译单元须统一配置；此值不检测机器的真实缓存行大小。
#ifndef QLIB_CACHE_LINE
#define QLIB_CACHE_LINE 64
#endif

namespace qlib {

// 价格、数量分别以 1/price_scale、1/qty_scale 为单位；调用方负责换算和溢出检查。
inline constexpr std::int64_t price_scale = 10'000'000'000;
inline constexpr std::int64_t qty_scale = 100'000'000;
inline constexpr std::uint8_t order_tag = 0;
inline constexpr std::uint8_t trade_tag = 1;
inline constexpr std::uint8_t level_tag = 2;
inline constexpr std::uint8_t cancel_tag = 3;
inline constexpr std::uint8_t snapshot_depth = 1;

enum class side : std::uint8_t { buy, sell };
enum class order_type : std::uint8_t { limit, market };
enum class order_action : std::uint8_t { add, modify };

// 六种记录由调用方先用 T{} 初始化，再填写业务字段；零值不代表业务有效。
// seq 来自消息源，instrument_id 来自调用方品种体系；库不分配标识或补齐数据。
// event_ns 为交易所事件时间，recv_ns 为本地接收时间，均为 Unix epoch 纳秒。
// 记录只保存字段，无业务校验、分配或自定义异常。并发写入由调用方同步。
// 字段采用本机布局；填充、字节序与指针表示不构成跨平台序列化协议。

// 保存订单新增或修改。add 使用 qty，modify 使用 new_qty（修改后的剩余量）。
// prev/next 借用本地订单地址；复制只复制地址，析构不释放邻居或解除链接。
// 调用方保证所指订单存活且地址有效，失效前解除引用；接收方须重建本地链接。
struct order {
  std::int64_t seq;
  std::int64_t order_id;
  std::int64_t price;
  std::int64_t qty;
  std::int64_t new_qty;
  order* prev;
  order* next;
  std::uint32_t instrument_id;
  qlib::side side;
  order_type type;
  order_action action;
  std::int64_t event_ns;
  std::int64_t recv_ns;
};

// qty 保存本次撤掉量；全撤时由调用方填写当前剩余量，记录不查找或修改订单。
struct cancel {
  std::int64_t seq;
  std::int64_t order_id;
  std::int64_t qty;
  std::uint32_t instrument_id;
  qlib::side side;
  std::int64_t event_ns;
  std::int64_t recv_ns;
};

// 保存成交事实；side 的来源映射及缺失订单 ID 的表示由调用方约定。
struct trade {
  std::int64_t seq;
  std::int64_t buy_order_id;
  std::int64_t sell_order_id;
  std::int64_t price;
  std::int64_t qty;
  std::int64_t event_ns;
  std::uint32_t instrument_id;
  qlib::side side;
  std::int64_t recv_ns;
};

// qty 保存更新后的价位绝对总量；qty <= 0 表达移除，实际更新由调用方执行。
struct level {
  std::int64_t seq;
  std::int64_t price;
  std::int64_t qty;
  std::int64_t event_ns;
  std::uint32_t instrument_id;
  qlib::side side;
  std::int64_t recv_ns;
};

// 保存调用方生成的快照副本；同侧同下标的价格和数量配对，最优档在前。
// 当前每侧一档；空档和缺失时间由调用方协议约定，快照不关联活跃价位指针。
struct book {
  std::int64_t event_ns;
  std::int64_t bid_price[snapshot_depth];
  std::int64_t bid_qty[snapshot_depth];
  std::int64_t ask_price[snapshot_depth];
  std::int64_t ask_qty[snapshot_depth];
  std::uint32_t instrument_id;
  std::int64_t recv_ns;
};

// 保存调用方采集的监控样本；普通整数不提供原子累加或一致采样。
// 累计值的统计起点、重置和丢弃口径由调用方约定。
struct metrics {
  std::int64_t ts_ns;                 // 样本时间，Unix epoch 纳秒。
  std::uint64_t feed_messages;        // 累计行情消息数。
  std::uint64_t feed_bytes;           // 累计行情字节数。
  std::uint64_t feed_orders;          // 累计订单记录数。
  std::uint64_t feed_trades;          // 累计成交记录数。
  std::uint64_t feed_levels;          // 累计价位更新记录数。
  std::uint64_t feed_dropped;         // 累计丢弃数。
  std::uint64_t book_events;          // 累计处理事件数。
  std::uint64_t book_apply_ns;        // 被采样的处理耗时之和，单位为纳秒。
  std::uint64_t book_samples;         // 上述耗时对应的采样次数。
  std::uint64_t book_instruments;     // 当前品种数。
  std::uint64_t book_resting_orders;  // 当前挂单数。
  std::uint64_t book_memory_bytes;    // 当前内存字节量。
  std::uint64_t writer_orders;        // 累计写出的订单记录数。
  std::uint64_t writer_trades;        // 累计写出的成交记录数。
  std::uint64_t writer_levels;        // 累计写出的价位记录数。
  std::uint64_t writer_books;         // 累计写出的快照数。
};

// 保存本地价位的总量及非拥有首尾指针；默认构造为空，不自动维护链接或求和。
// 调用方维护同侧同价订单关系，并在订单销毁或地址失效前解除引用。
// 复制只复制数量和地址，析构不释放订单；并发修改由调用方同步。
struct price_level {
  std::int64_t qty = 0;
  order* head = nullptr;
  order* tail = nullptr;
};

static_assert(std::is_trivially_copyable_v<order> && std::is_standard_layout_v<order>);
static_assert(std::is_trivially_copyable_v<cancel> && std::is_standard_layout_v<cancel>);
static_assert(std::is_trivially_copyable_v<trade> && std::is_standard_layout_v<trade>);
static_assert(std::is_trivially_copyable_v<level> && std::is_standard_layout_v<level>);
static_assert(std::is_trivially_copyable_v<book> && std::is_standard_layout_v<book>);
static_assert(std::is_trivially_copyable_v<metrics> && std::is_standard_layout_v<metrics>);

// 集合按值保存记录；标签是独立约定，variant 的内存表示不能用作消息格式。
using feed_event = std::variant<order, trade, level, cancel>;
using record = std::variant<order, trade, level, cancel, book>;

// 组合可调用对象供 std::visit 选择；捕获引用须保持有效，异常按调用方函数传播。
// 构造与调用的资源成本、外部状态变化和线程同步由各可调用对象决定。
template <typename... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

}  // namespace qlib
