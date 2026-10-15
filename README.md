# qlib

qlib 为 nqbook 等 C++ 项目提供共享数据定义和基础容器。本工作区完成的开发阶段为
**V0：工程接入与公共数据契约**，当前可用组件是 `<qlib/common.h>`。
`hive`、`map`、`single_queue`、`pool`、`memory_pool` 留在后续版本实现。

库采用 C++23、纯头文件结构，运行时仅依赖标准库。`qlib::qlib` 是 CMake 接口目标，
向调用方传递头文件目录和语言要求，本身不生成库二进制或服务进程。

## 公共定义

| 定义 | 用途与约定 |
| --- | --- |
| `order` | 新增或修改订单；`add` 读取 `qty`，`modify` 读取修改后剩余量 `new_qty`。 |
| `cancel` | 撤单；`qty` 是本次撤掉量，全撤时由调用方填写当前剩余量。 |
| `trade` | 成交事实，包含买卖订单 ID、成交价量；方向及缺失 ID 的映射由接入方约定。 |
| `level` | L2 价位更新；`qty` 是更新后的绝对总量，`qty <= 0` 表达移除价位。 |
| `book` | 调用方生成的快照；四个原生数组各一项，买卖价量按同侧同下标配对，最优档在前。 |
| `metrics` | 17 字段监控样本；保存采样时间、累计计数、采样耗时和当前状态。 |
| `price_level` | 本地价位总量和非拥有订单首尾指针；默认 `qty=0`，`head/tail=nullptr`。 |
| `feed_event` | 按顺序容纳 `order, trade, level, cancel`。 |
| `record` | 按顺序容纳 `order, trade, level, cancel, book`。 |
| `overloaded` | 组合 lambda，供 `std::visit` 按活动类型选择处理函数。 |

价格单位为 `1/price_scale`，其中 `price_scale=10^10`；数量单位为 `1/qty_scale`，
其中 `qty_scale=10^8`。换算、舍入与溢出检查由调用方负责。
`event_ns` 为交易所事件时间，`recv_ns` 为本地接收时间，`metrics.ts_ns` 为采样时间，
均采用 Unix epoch 纳秒。

三个枚举 `side { buy, sell }`、`order_type { limit, market }`、
`order_action { add, modify }` 的底层类型均为 `std::uint8_t`，各自数值依次为 0、1。
四个 `std::uint8_t` 标签 `order_tag/trade_tag/level_tag/cancel_tag` 分别为 0、1、2、3；
`snapshot_depth` 为 `std::uint8_t` 常量 1。`metrics` 和 `price_level` 均不进入两个 variant。

六种记录没有成员默认值，使用时先写 `T value{}`，再填写业务字段。
零初始化只提供确定初值，不定义合法订单、空快照或缺失时间。
记录不会自动校验业务、修改订单簿、生成快照或采集指标。
`metrics.book_apply_ns` 是采样耗时之和，`book_samples` 是对应次数；
`book_instruments/book_resting_orders/book_memory_bytes` 是当前状态，其他计数为累计值。
统计起点与重置由调用方约定。

`order.prev/next`、`price_level.head/tail` 只借用本进程中仍存活且地址有效的订单。
复制只复制地址；销毁记录不释放订单，也不自动解除链接。
调用方在订单销毁或地址失效前解除引用，并自行维护链接和价位总量。
只维护聚合量时，可以保留空首尾指针。

记录字段没有同步机制；无写入时可并发只读，涉及写入由调用方同步。
`overloaded` 保存可调用对象，引用捕获须保持有效；构造或调用异常按用户函数及标准库规则传播。
错误的 `std::get<T>` 访问遵循 `std::bad_variant_access`，库不包装为其他错误码。

六种记录保留 trivially-copyable 和 standard-layout 静态断言。
这些属性不保证跨 ABI 的字节兼容性；结构体填充、字节序和指针表示仍与平台有关。
接收方须建立本地订单链接，不使用发送方指针；`price_level` 不作为线缆记录发送，
variant 的内存表示也不能直接当作消息格式。
修改字段、标签、类型集合或快照深度时，需要检查调用方兼容性并统一重编译。

`QLIB_CACHE_LINE` 默认 64，可在构建配置中统一覆盖。
同一程序各翻译单元必须采用相同值；该配置不检测机器缓存行，也不提供无锁保证。

## 构建和测试

需要支持 C++23 的 GCC 或 Clang、CMake 3.28+、Git 和本地构建工具。
测试开启时通过 FetchContent 获取 GoogleTest v1.17.0，首次配置需要网络。

在仓库根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQLIB_BUILD_TESTS=ON
cmake --build build --parallel
./build/tests/common_test
ctest --test-dir build -N
ctest --test-dir build --output-on-failure --no-tests=error
```

顶层项目的 `QLIB_BUILD_TESTS` 默认 ON，作为子项目时默认 OFF。
显式传入 `-DQLIB_BUILD_TESTS=OFF` 会关闭测试目标及 GoogleTest 获取流程。
检查默认值时使用新构建目录，避免既有缓存干扰。
`build/compile_commands.json` 由 CMake 生成。
配置成功和零测试均不算功能验证通过。

## 调用工程

假设 qlib 源码位于调用工程的 `qlib/` 子目录：

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES CXX)
add_subdirectory(qlib)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE qlib::qlib)
```

`main.cpp` 只包含公共头，明确填写一条撤单记录并检查读取结果：

```cpp
#include <qlib/common.h>

int main() {
  qlib::cancel value{};
  value.seq = 7;
  value.order_id = 42;
  value.qty = 2 * qlib::qty_scale;
  value.instrument_id = 1;
  value.side = qlib::side::buy;
  value.event_ns = 1'000;
  value.recv_ns = 1'100;
  const qlib::feed_event event{value};
  const bool ok = std::visit(qlib::overloaded{
    [](const qlib::order&) { return false; },
    [](const qlib::trade&) { return false; },
    [](const qlib::level&) { return false; },
    [](const qlib::cancel& c) {
      return c.seq == 7 && c.order_id == 42 && c.qty == 200'000'000 &&
             c.instrument_id == 1 && c.side == qlib::side::buy &&
             c.event_ns == 1'000 && c.recv_ns == 1'100;
    }
  }, event);
  return ok ? 0 : 1;
}
```

该检查在 Release 中仍执行；它验证数据保存和类型分派，不执行撤单业务。
可复用的独立工程源码见 [v0-consumer](docs/validation/v0-consumer/CMakeLists.txt)。
本阶段提供 `add_subdirectory` 接入，不包含 install/package 或 CI。

## V0 验证记录

2026-10-15 在 WSL2 x86-64、Ubuntu 24.04.4、GCC 13.3.0、CMake 3.28.3 下完成 Release 验证。
common_test 与 CTest 均通过 17 个用例（0 失败、0 跳过）；独立调用及测试开关矩阵通过。
项目统一命名为 `qlib` 后，重新编译并运行了相同的 17 项测试；独立调用程序也编译并退出 0。
完整命令、测试数量、独立调用和测试开关检查见
[V0-T01 验收记录](docs/validation/V0-T01.md)。
当前工作区未初始化 Git，交付状态以该记录中的 SHA-256 清单识别。
V1—V4 未开始，性能、并发及其他平台未验证。

开发范围见 [Sprint Backlog](docs/Sprint_Backlog.md)，字段与行为以
[需求](docs/requirement.md) 和 [规格](docs/specs.md) 为准。
实施步骤见 [V0 计划](<docs/Implementation_Plan (V0).md>)。
公共定义参考 qlib v0.5.1；保留参考项目的 [Apache 2.0 许可证](LICENSE)。
