# qlib

qlib 为 nqbook 等 C++ 项目提供共享数据定义和基础容器。
提供公共数据 `<qlib/common.h>`、地址稳定的对象存储 `<qlib/hive.h>`、
键值索引 `<qlib/map.h>`、固定块内存池 `<qlib/memory_pool.h>`、句柄对象池 `<qlib/pool.h>`
和单生产者、单消费者队列 `<qlib/single_queue.h>`。

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

## 对象池与原始内存池

| 组件 | 返回什么 | 谁负责对象 | 容量与复用 |
| --- | --- | --- | --- |
| `memory_pool` | `void*` 原始块地址 | 调用方构造对象，并在归还前析构对象。 | 构造时固定块数；耗尽返回 `nullptr`，最近归还的块先借出。 |
| `pool<T>` | `size_type` 槽位句柄 | 池通过 `emplace` 构造，`release` 或池析构销毁。 | 构造参数仅为预留量，空间不足可以增长；最近释放的槽先复用。 |

两种池各自管理存储，必须带参数构造，均不可复制、不可移动，没有 `clear/reset`。
`empty()` 表示当前没有借出块或存活对象；`size()` 是对应数量。
固定池借满时 `empty()` 为 false，能否借到下一块要看 `allocate()` 的返回值。

`memory_pool(block_size, capacity, alignment)` 默认使用 `alignof(std::max_align_t)`。
实际对齐至少为 `alignof(void*)`，实际块大小至少为 `sizeof(void*)`，并向实际对齐的倍数取整。
两个查询 `block_size()`、`alignment()` 返回实际值；请求块大小与容量都允许为 0。
对齐必须是平台支持的非零二次幂，取整与总字节数必须可表示，调用方负责满足这些条件。
即使容量为 0，构造仍调用对齐分配，分配异常会向外传播。

固定池创建后，借还只更新地址、链接和计数，不为池自身申请新存储。
归还地址必须是本池仍借出的块首地址，不能为 nullptr、偏移地址或重复归还。
块不会自动清零，归还会覆盖块头；销毁池会释放整段存储，但不会替调用方析构块内对象。
下面的程序先清理字符串，再归还它所在的块；构造失败时只归还原始块。

```cpp
#include <qlib/memory_pool.h>
#include <memory>
#include <string>

int main() {
  qlib::memory_pool storage(sizeof(std::string), 1, alignof(std::string));
  void* raw = storage.allocate();
  if (raw == nullptr) return 1;
  std::string* value = nullptr;
  try {
    value = std::construct_at(static_cast<std::string*>(raw), "hello");
  } catch (...) {
    storage.deallocate(raw);
    throw;
  }
  const bool correct = *value == "hello";
  std::destroy_at(value);
  storage.deallocate(raw);
  return correct && storage.empty() ? 0 : 2;
}
```

示例中的使用步骤不抛异常。业务处理若可能抛异常，调用方要用作用域清理保证所有退出路径
都先析构对象、再归还块。销毁池前可以不逐块归还原始内存，但必须完成对象自身的清理。

`pool<T>(capacity)` 只预留槽空间，不提前构造 T；预留 0 后仍可通过 `emplace` 增长。
T 的移动构造与析构必须不抛异常；这属于使用条件，模板不新增统一的类型拒绝检查。
指定参数的新对象构造可以抛异常：复用失败恢复原空闲链，追加失败移除临时末槽，存活数不增加。
已经完成的增长和地址变化可以保留；每次 `emplace` 成功或失败后，都应凭有效句柄重新取得引用。
如果构造参数来自池内对象，先在池外暂存所需值，避免增长后继续读取已失效的实参引用。

```cpp
#include <qlib/pool.h>
#include <string>

int main() {
  qlib::pool<std::string> objects(1);
  const auto first = objects.emplace("first");
  const auto second = objects.emplace("second");
  const bool preserved = objects[first] == "first";
  objects.release(first);
  const auto replacement = objects.emplace("replacement");
  const bool reused = replacement == first && objects[replacement] == "replacement";
  objects.release(replacement);
  objects.release(second);
  return preserved && reused && objects.empty() ? 0 : 1;
}
```

释放后的旧句柄失效；相同数字再次返回时，调用方保存本次返回的新句柄。
`release` 销毁对象并保留容量；池析构清理剩余存活对象和全部存储。
池不会诊断越界、过期或跨池句柄，不支持重复释放或移动对象抛异常后的通用恢复。
复用和访问的管理步骤为常数，增长要搬迁已有槽位；这些性质不代表性能实测结果。

两种池没有内部锁。无写入时可以并发只读；修改、释放和销毁由调用方同步。
同步须覆盖从取得地址或引用到使用结束的整个期间，用户构造、移动和析构也不能重入同一池。
两个头文件分别接入的完整工程见本地 [v2-consumer](docs/validation/v2-consumer/CMakeLists.txt)。

## 线程间有序交接

`single_queue<T>` 只允许一个生产者入队、一个消费者出队，成功放入的对象按 FIFO 顺序取出。
请求容量 0、1、5、8、9 时，实际容量分别为 1、1、8、8、16。槽位只在构造时分配，
运行期间容量固定；所有槽位都可用。队列只能带容量构造，不能复制或移动。

`try_emplace(args...)` 在槽位原地构造，`try_push(T&&)` 转交已有值。
满时返回 `false`，函数内部不会构造新对象或移动输入；调用方的实参表达式仍会先求值。
普通参数构造失败时异常向外传播，不发布新项，之后可以继续使用。
`try_pop()` 空时返回 `std::nullopt`；成功时返回的 `std::optional<T>` 独立拥有值，
原槽位对象已销毁。返回结果在槽位复用或队列销毁后仍有效。

下面用单线程展示所有权交接和满时重试；双线程运行使用后文的停止与销毁规则。

```cpp
#include <qlib/single_queue.h>
#include <memory>
#include <utility>

int main() {
  qlib::single_queue<std::unique_ptr<int>> queue(1);
  if (!queue.try_emplace(std::make_unique<int>(7))) return 1;
  auto next = std::make_unique<int>(42);
  if (queue.try_push(std::move(next)) || !next) return 1;
  auto first = queue.try_pop();
  if (!first || !*first || **first != 7) return 1;
  if (!queue.try_push(std::move(next)) || next) return 1;
  auto second = queue.try_pop();
  return second && *second && **second == 42 && queue.empty() ? 0 : 1;
}
```

`T` 不需要默认构造或复制，但从 `T&&` 构造和析构必须实际不抛异常。
这些操作可以没有 `noexcept` 声明；队列不增加模板拒绝规则。
调用方保证容量取整后可由 `size_t` 和 `uint64_t` 表示且小于 `2^64`，
总字节数不溢出，并满足有效分配条件；队列没有完整的溢出检查。

生产者完成对象构造后，以 release 发布 `tail`，消费者 acquire 接收后读取对象。
消费者移出并销毁槽位对象后，以 release 发布 `head`，生产者 acquire 接收后复用槽位。
`tail/cached_head` 由生产者更新，`head/cached_tail` 由消费者更新；每侧只使用自己缓存的
已确认范围。单次管理操作没有等待对侧推进的循环，满或空时直接返回。
无锁结论还要求目标平台的原子实现支持；用户对象内部的分配、锁和等待另计。

运行时 `size/empty` 只供生产者或消费者观察，不能代替入出队返回值或作为完成信号。
第三方线程只能在访问结束并完成同步后查询。双方不得同侧重入；交接线程角色需要外部同步。
调用方先创建队列，再启动两个线程，用独立的同步停止状态协调退出，最后 `join` 两侧，
再读取最终结果和销毁队列。队列析构只清理剩余对象，不会替调用方停止线程。
含截止时间、异常捕获和部分启动失败收尾的完整双线程示例见本地
[v3-consumer](docs/validation/v3-consumer/main.cpp)，测试中的
[SingleQueueConcurrent](tests/single_queue_test.cpp) 还验证主动停止与失败路径。

## 事件、订单存储和索引组合

`single_queue<feed_event>` 传递事件值；唯一消费者独占 `hive<order>` 和
`map<std::int64_t, order*>`。生产者不访问订单存储，也不传递可供另一侧解引用的订单地址。
索引中的地址只借用 hive 里仍存活的订单；两个容器不自动组成事务。

新增时先在 hive 构造候选，再建立索引。重复 ID 保留旧对象、旧字段和旧映射，回收新候选；
索引提交前抛异常时，同样回收候选并继续传播异常。删除时先移除索引，再销毁订单。
双方完成并 join 后，清空时也先 `by_id.clear()`，再 `orders.clear()`。

本版样例只处理新增和全撤：撤单须匹配 ID、品种、方向与全部剩余量；数量不符保持原单。
`modify`、成交和价位事件只验证字段交接，不更新订单。完整的十事件输入、独立预期及
单/双线程代码见 [EventStorageIntegration](tests/single_queue_test.cpp)。
这套规则用于演示组件协作，业务程序须自行确定订单处理规则。

满时重试同一个事件，成功后才推进发送序号。正常结束使用独立的生产者完成标志 `done`
发布完成状态；消费者 acquire 观察完成后再次出队，确认最后的发布没有被早先空观察漏掉。
异常、主动停止或截止时间到达时，两侧停止并 join，随后报告已经处理的数量；
队列析构清理剩余元素不代表业务处理成功。主线程不在工作期间通过 `size/empty` 判断完成。

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
./build/tests/memory_pool_test
./build/tests/pool_test
./build/tests/single_queue_test
ctest --test-dir build -N
ctest --test-dir build --output-on-failure --no-tests=error
```

顶层项目的 `QLIB_BUILD_TESTS` 默认 ON，作为子项目时默认 OFF。
显式传入 `-DQLIB_BUILD_TESTS=OFF` 会关闭测试目标及 GoogleTest 获取流程。
常规验证只复用一个 `build/`；仅在修改构建配置或排错时额外检查测试开关。
ASan/UBSan 与 TSan 分别使用独立临时构建以隔离检查选项，完成后清理本次新建的临时产物。
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

调用程序创建线程时，另用 `find_package(Threads REQUIRED)` 查找线程支持，
再把 `Threads::Threads` 链接到调用目标。库本身不创建线程。

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
六个公共头分别使用的最小工程见本地 [v4-consumer](docs/validation/v4-consumer/CMakeLists.txt)。
复制该目录到库源码树外，配置时传入 `-DQLIB_SOURCE_DIR=/path/to/qlib`，编译后运行
`qlib_consumer_v4`。六项检查始终全部执行，失败返回非零；不靠 Release 中可能关闭的 `assert`。
六个检查分别编译，只链接 `qlib::qlib`，没有手动补充头文件路径或 C++ 标准。
本阶段提供 `add_subdirectory` 接入，不包含 install/package 或 CI。

## map 性能基准

启用测试的 Release 构建会生成独立的 `map_bench`，它只链接 `qlib::qlib`。
CTest 不运行这个程序，需要另行执行：

```bash
./build/tests/map_bench
```

比较 `qlib::map<int,int>` 与 `std::unordered_map<int,int>` 的 insert、find_hit、find_miss、erase。
双方默认配置、不提前 reserve，共用一百万个键；`mt19937(42)` 依次打乱命中键和未命中键。
每项测三次取最小值，单位 ns/op。输入、预填和析构在相应计时外；插入增长计入，
查询和删除的结果累加也计入。stdout 是八行三列无表头 CSV，stderr 应为 `sink=3000009000000`。
测量时单独运行，保存两个输出通道、退出码和环境。功能正确性由测试另行验证。

2026-10-20，i7-13650HX / WSL2 / GCC 13.3 / libstdc++，Release `-O3 -DNDEBUG`：

| 操作 | qlib::map ns/op | std::unordered_map ns/op |
| --- | ---: | ---: |
| insert | 38.0 | 105.4 |
| find_hit | 14.2 | 13.7 |
| find_miss | 12.4 | 23.0 |
| erase | 16.3 | 37.3 |

本次命中查询标准库略快，其余三项 qlib 较快。三次最小值不能表示 p99、最坏单次延迟
或其他负载的性能；本次未绑定 CPU，也没有隔离整个系统的后台负载。
原始结果、计时范围和复现条件见本地 [V4 验收记录](docs/validation/V4.md)。

## 验证记录

V4 于 2026-10-20 完成，工作区标识 `qlib-v4-20261020`；V0—V4 共 10 项任务、5 个版本完成。
本次先重跑原有 142 项，再加入 9 项 EventStorageIntegration。最终全新 Release 和
启用泄漏检测的 ASan/UBSan 各通过 151/151；TSan 覆盖队列及事件组合，通过 35/35，
均无失败、跳过或工具报告。TSan 普通启动的地址映射错误在无 qlib 探针中复现，
对相应进程使用 `setarch x86_64 -R` 后通过。
四种全新构建配置、外部六头独立调用及八行基准格式检查均通过。
`atomic<uint64_t>` 编译期与运行期无锁检查均为 true；同步和固定存储结论另经代码审查。
完整命令、原始性能数据、源码 SHA-256、16 项 FR 与 8 项 NFR 的证据对应关系见本地
[V4 验收记录](docs/validation/V4.md)。没有剩余必需项或阻塞；其他平台和生产尾延迟未验证。
本次新增临时构建、依赖和探针用完清理，保留必要的手写源码和规定的测量证据。


V3 于 2026-10-19 完成，工作区标识 `qlib-v3-20261019`。开发前重跑 116 项旧测试，
新增 26 项队列测试后，Release 与启用泄漏检测的 ASan/UBSan 均通过 142/142；
TSan 队列测试通过 26/26，均无失败、跳过或工具报告。TSan 首次遇到运行时地址映射错误，
对构建及测试进程使用 `setarch x86_64 -R` 后通过，没有更改系统设置。
每轮完整交接 180 万项多字段载荷和 45 万项独占资源；主动停止及异常收尾也通过。
独立消费工程完成 10000 项交接，目标 `atomic<uint64_t>` 编译期与运行期检查均为无锁。
环境仍为 GCC 13.3.0 / libstdc++、CMake 3.28.3、GoogleTest v1.17.0、WSL2 x86-64。
完整命令、首次失败与修复条件、范围限制见本地 [V3 验收记录](docs/validation/V3.md)。

V2 于 2026-10-16 完成，工作区标识为 `qlib-v2-20261016`。
本次先实际重跑 V0/V1 的 75 项测试，再加入 memory_pool 的 27 项和 pool 的 14 项。
最终 Release 和 Debug ASan/UBSan（开启泄漏检测）均通过全部 116 项，0 失败、0 跳过，
未发现工具报错。两个新增头文件的独立调用程序编译、运行均成功；头文件路径与 C++23
要求由 `qlib::qlib` 传入，子项目默认关闭测试且不引入 GoogleTest。
实际环境为 WSL2 x86-64、Ubuntu 24.04.4、GCC 13.3.0、CMake 3.28.3、GoogleTest v1.17.0。
命令、覆盖范围和检查限制见本地 [V2 验收记录](docs/validation/V2.md)。2026-10-16 的开发验收未包含 Git 提交或推送。

V1 于 2026-10-15 完成：Release 和 Debug ASan/UBSan（含泄漏检查）均通过 75 项测试，
0 失败、0 跳过。包括 V0 的 17 项、hive 的 18 项、map 的 33 项和组合场景的 7 项。
两个独立头消费程序均通过 `qlib::qlib` 编译并运行成功；子项目默认关闭测试且不引入 GoogleTest。
V1 开发验收时的工作区标识为 `qlib-v1-20261015`。

2026-10-15 在 WSL2 x86-64、Ubuntu 24.04.4、GCC 13.3.0、CMake 3.28.3 下完成 Release 验证。
common_test 与 CTest 均通过 17 个用例（0 失败、0 跳过）；独立调用及测试开关矩阵通过。
项目统一命名为 `qlib` 后，重新编译并运行了相同的 17 项测试；独立调用程序也编译并退出 0。
完整命令、测试数量、独立调用和测试开关检查见
[V0-T01 验收记录](docs/validation/V0-T01.md)。
该记录描述 V0 当时的工作区和验收状态。
V1 验收结果见本地 [V1 验收记录](docs/validation/V1.md)。
`docs/validation/` 保存手写调用源码和简短验证记录，并按项目约定不上传 GitHub。
V4 的本次验收结果见上方记录；其他平台仍未验证。

开发范围见 [Sprint Backlog](docs/Sprint_Backlog.md)，字段与行为以
[需求](docs/requirement.md) 和 [规格](docs/specs.md) 为准。
实施步骤见 [V0 计划](<docs/Implementation_Plan (V0).md>)、[V1 计划](docs/Implementation_Plan(V1).md)、
[V2 计划](<docs/Implementation_Plan (V2).md>)、[V3 计划](<docs/Implementation_Plan (V3).md>)
和 [V4 计划](<docs/Implementation_Plan (V4).md>)。
公共定义参考 qlib v0.5.1；保留参考项目的 [Apache 2.0 许可证](LICENSE)。
