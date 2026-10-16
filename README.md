# qlib

qlib 为 nqbook 等 C++ 项目提供共享数据定义和基础容器。
已完成 V1，在公共数据 `<qlib/common.h>` 的基础上增加 `<qlib/hive.h>` 和 `<qlib/map.h>`，
用于保存地址稳定的对象，以及按唯一键查找数据。
`single_queue`、`pool`、`memory_pool` 留在后续版本实现。

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

## 对象存储和键值索引

`hive<T>` 拥有对象。新增和删除其他对象不会改变仍存活对象的地址，删除留下的空位可以复用。
它支持原地构造、右值插入、单项/范围删除、双向及 const 遍历、有效地址定位、移动和交换。
不保证遍历顺序，也没有随机下标或 `reserve`。`clear()` 销毁对象并释放全部容量。
对象可以不能复制、不能移动，只要能用 `emplace` 的参数构造且析构不抛异常。
构造失败不增加存活数量；已经申请的空尾块和容量可能保留。

`map<Key,T>` 拥有唯一键值对，默认使用 `std::hash<Key>` 和 `std::equal_to<Key>`。
它支持插入、查询、删除、前向及 const 遍历、`reserve`、移动和交换；`clear()` 同样释放容量。
`value_type` 是 `std::pair<Key,T>`，可以修改 `second`，**不得修改已存储的 `first`**。
整数键 0 是普通键；缺键的 `find/contains/erase` 不创建元素，`at` 抛出 `std::out_of_range`。
`operator[]` 缺键时创建值初始化的值；`reserve(n)` 的 n 是预计总数。

`insert` 只接收右值 pair，`try_emplace` 和 `operator[]` 只接收右值键。
命名键需要显式复制成临时键或移动，例如 `index.try_emplace(std::int64_t{id}, pointer)`。
重复的 `insert/try_emplace` 保留旧值且不在函数内部移动实参；`emplace` 会先构造临时 pair。
新值构造失败不会增加数量，但此前完成的重哈希可能已经改变容量和元素地址。
map 的 pair 迁移、析构、哈希、比较及实际使用到的策略构造/移动/交换必须不抛异常。
不要求所有这些操作都在类型声明上标注 `noexcept`；违反行为约束不保证恢复。

两个容器都不能复制。hive 正向元素迭代器在增删其他元素后保持有效，删除目标后其位置失效；
修改后重新取得反向迭代器，移动/交换后重新取得全部迭代器。
`get_iterator` 只保证定位本容器当前存活对象的准确地址，不能检查任意旧地址是否安全。
map 插入或重哈希后重新查询已有项；删除只使被删项失效，移动/交换后重新取得位置。
可能触发 map 增长时，先把所需参数保存在容器外，不能继续依赖可能被搬走的内部引用。

两者都要求容量计算可表示，`max_size()` 是粗略上界，不代表所有溢出都已检查。
没有内部锁；共享只读还要求元素和策略自身安全，写入、移动、交换和销毁须由调用方同步。
hive 常规增删的管理步骤为常数，扩块需要初始化，地址定位需要扫描块；
map 查询平均为常数、严重冲突时最坏扫描容量，重哈希会搬迁元素，迭代和删除后继也可能扫描容量。
这些说明不是性能实测结论。

下面把订单交给 hive 管理，让 map 只保存 ID 和地址。重复或建立索引失败时回收新订单；
正常删除先移除索引，再销毁订单。两个容器不会自动共同回滚。

```cpp
#include <qlib/common.h>
#include <qlib/hive.h>
#include <qlib/map.h>
#include <memory>

int main() {
  qlib::hive<qlib::order> orders;
  qlib::map<std::int64_t, qlib::order*> by_id;
  qlib::order input{};
  input.order_id = 42;
  input.instrument_id = 1;
  input.side = qlib::side::buy;
  input.type = qlib::order_type::limit;
  input.action = qlib::order_action::add;
  input.price = 100 * qlib::price_scale;
  input.qty = 2 * qlib::qty_scale;
  input.event_ns = 1'000;
  input.recv_ns = 1'100;

  auto pos = orders.emplace(input);
  bool inserted = false;
  try {
    inserted = by_id.try_emplace(std::int64_t{input.order_id},
                                 std::addressof(*pos)).second;
  } catch (...) {
    orders.erase(pos);
    throw;
  }
  if (!inserted) {
    orders.erase(pos);
    return 1;
  }

  const auto found = by_id.find(42);
  auto* resting = found->second;
  const bool correct = resting->qty == 200'000'000;
  by_id.erase(found);
  orders.erase(orders.get_iterator(resting));
  return correct && by_id.empty() && orders.empty() ? 0 : 2;
}
```

这里没有建立订单前后链；业务中若还保存其他借用指针，也须在对象销毁前解除。
先声明 `orders`、后声明 `by_id`，退出作用域时会先销毁索引，再销毁订单。

## 构建和测试

需要支持 C++23 的 GCC 或 Clang、CMake 3.28+、Git 和本地构建工具。
测试开启时通过 FetchContent 获取 GoogleTest v1.17.0，首次配置需要网络。

在仓库根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQLIB_BUILD_TESTS=ON
cmake --build build --parallel
./build/tests/common_test
./build/tests/hive_test
./build/tests/map_test
ctest --test-dir build -N
ctest --test-dir build --output-on-failure --no-tests=error
```

顶层项目的 `QLIB_BUILD_TESTS` 默认 ON，作为子项目时默认 OFF。
显式传入 `-DQLIB_BUILD_TESTS=OFF` 会关闭测试目标及 GoogleTest 获取流程。
常规验证只复用一个 `build/`；仅在修改构建配置或排错时额外检查测试开关。
ASan/UBSan 使用单独临时构建以隔离编译选项，完成后清理本次新建的临时产物。
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

## 验证记录

V1 于 2026-10-15 完成：Release 和 Debug ASan/UBSan（含泄漏检查）均通过 75 项测试，
0 失败、0 跳过。包括 V0 的 17 项、hive 的 18 项、map 的 33 项和组合场景的 7 项。
两个独立头消费程序均通过 `qlib::qlib` 编译并运行成功；子项目默认关闭测试且不引入 GoogleTest。
当前工作区标识为 `qlib-v1-20261015`，本次未提交或推送 Git。

2026-10-15 在 WSL2 x86-64、Ubuntu 24.04.4、GCC 13.3.0、CMake 3.28.3 下完成 Release 验证。
common_test 与 CTest 均通过 17 个用例（0 失败、0 跳过）；独立调用及测试开关矩阵通过。
项目统一命名为 `qlib` 后，重新编译并运行了相同的 17 项测试；独立调用程序也编译并退出 0。
完整命令、测试数量、独立调用和测试开关检查见
[V0-T01 验收记录](docs/validation/V0-T01.md)。
该记录描述 V0 当时的工作区和验收状态。
V1 验收结果见本地 [V1 验收记录](docs/validation/V1.md)。
`docs/validation/` 保存手写调用源码和简短验证记录，并按项目约定不上传 GitHub。
V2—V4 未开始，性能、并发及其他平台未验证。

开发范围见 [Sprint Backlog](docs/Sprint_Backlog.md)，字段与行为以
[需求](docs/requirement.md) 和 [规格](docs/specs.md) 为准。
实施步骤见 [V0 计划](<docs/Implementation_Plan (V0).md>) 和 [V1 计划](docs/Implementation_Plan(V1).md)。
公共定义参考 qlib v0.5.1；保留参考项目的 [Apache 2.0 许可证](LICENSE)。
