#include <qlib/common.h>
#include <qlib/hive.h>
#include <qlib/map.h>
#include <qlib/single_queue.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using int_queue = qlib::single_queue<int>;
static_assert(std::same_as<int_queue::value_type, int>);
static_assert(std::same_as<int_queue::size_type, std::size_t>);
static_assert(!std::is_default_constructible_v<int_queue>);
static_assert(!std::is_copy_constructible_v<int_queue>);
static_assert(!std::is_copy_assignable_v<int_queue>);
static_assert(!std::is_move_constructible_v<int_queue>);
static_assert(!std::is_move_assignable_v<int_queue>);
static_assert(!std::is_convertible_v<std::size_t, int_queue>);
static_assert(std::same_as<decltype(&int_queue::try_push), bool (int_queue::*)(int&&)>);
static_assert(std::same_as<decltype(std::declval<int_queue&>().try_emplace(1)), bool>);
static_assert(std::same_as<decltype(std::declval<int_queue&>().try_push(1)), bool>);
static_assert(std::same_as<decltype(std::declval<int_queue&>().try_pop()), std::optional<int>>);
static_assert(std::same_as<decltype(std::declval<const int_queue&>().capacity()), std::size_t>);
static_assert(std::same_as<decltype(std::declval<const int_queue&>().size()), std::size_t>);
static_assert(std::same_as<decltype(std::declval<const int_queue&>().empty()), bool>);
static_assert(noexcept(std::declval<const int_queue&>().capacity()));
static_assert(noexcept(std::declval<const int_queue&>().size()));
static_assert(noexcept(std::declval<const int_queue&>().empty()));
static_assert(!noexcept(std::declval<int_queue&>().try_emplace(1)));
static_assert(!noexcept(std::declval<int_queue&>().try_push(1)));
static_assert(!noexcept(std::declval<int_queue&>().try_pop()));
static_assert(std::is_nothrow_destructible_v<int_queue>);

struct lifetime_log {
  std::array<unsigned, 1024> destructions{};
  std::size_t attempts = 0;
  std::size_t created = 0;
  std::size_t destroyed = 0;
  std::size_t alive = 0;
  bool invalid_destruction = false;

  std::size_t construct() noexcept {
    ++alive;
    return ++created;
  }

  void destroy(std::size_t instance) noexcept {
    if (instance == 0 || instance >= destructions.size() || alive == 0) {
      invalid_destruction = true;
      return;
    }
    ++destructions[instance];
    ++destroyed;
    --alive;
  }

  void expect_balanced() const {
    EXPECT_FALSE(invalid_destruction);
    EXPECT_EQ(alive, 0u);
    EXPECT_EQ(destroyed, created);
    ASSERT_LT(created, destructions.size());
    for (std::size_t instance = 1; instance <= created; ++instance) {
      EXPECT_EQ(destructions[instance], 1u) << "instance=" << instance;
    }
  }
};

struct tracked {
  lifetime_log* log;
  int value;
  std::size_t instance;

  tracked(lifetime_log& events, int number, bool fail = false)
      : log(&events), value(number), instance(0) {
    ++log->attempts;
    if (fail) throw std::runtime_error("对象构造失败");
    instance = log->construct();
  }

  tracked(const tracked&) = delete;
  tracked& operator=(const tracked&) = delete;
  tracked(tracked&& other) noexcept
      : log(other.log), value(std::exchange(other.value, -1)), instance(log->construct()) {}
  ~tracked() { log->destroy(instance); }
};

static_assert(!std::is_default_constructible_v<tracked>);
static_assert(!std::is_copy_constructible_v<tracked>);
static_assert(std::is_nothrow_move_constructible_v<tracked>);
static_assert(std::is_nothrow_destructible_v<tracked>);

struct alignas(256) aligned_value {
  int value;

  aligned_value(std::vector<std::uintptr_t>& addresses, int number) : value(number) {
    addresses.push_back(reinterpret_cast<std::uintptr_t>(this));
  }
  aligned_value(aligned_value&&) noexcept = default;
};

struct move_without_noexcept {
  int value;

  explicit move_without_noexcept(int number) : value(number) {}
  move_without_noexcept(const move_without_noexcept&) = delete;
  move_without_noexcept(move_without_noexcept&& other) : value(other.value) {}
};

static_assert(!std::is_nothrow_move_constructible_v<move_without_noexcept>);

TEST(SingleQueue, SQ01RoundsCapacityAndStartsEmpty) {
  constexpr std::array<std::pair<std::size_t, std::size_t>, 5> cases{{
      {0, 1}, {1, 1}, {5, 8}, {8, 8}, {9, 16},
  }};
  for (const auto& [requested, expected] : cases) {
    SCOPED_TRACE(requested);
    const int_queue queue(requested);
    EXPECT_EQ(queue.capacity(), expected);
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());
    int_queue empty_queue(requested);
    EXPECT_EQ(empty_queue.try_pop(), std::nullopt);
    EXPECT_EQ(empty_queue.size(), 0u);
  }
}

TEST(SingleQueue, SQ02UsesEverySlotAndPreservesFullQueueOrder) {
  for (const std::size_t requested : {0u, 1u, 5u, 8u, 9u}) {
    SCOPED_TRACE(requested);
    int_queue queue(requested);
    const auto capacity = queue.capacity();
    for (std::size_t i = 0; i < capacity; ++i) {
      ASSERT_TRUE(queue.try_emplace(static_cast<int>(i)));
      EXPECT_EQ(queue.size(), i + 1);
      EXPECT_FALSE(queue.empty());
    }
    EXPECT_FALSE(queue.try_emplace(1000));
    EXPECT_FALSE(queue.try_push(2000));
    EXPECT_EQ(queue.size(), capacity);
    EXPECT_EQ(queue.capacity(), capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
      const auto value = queue.try_pop();
      ASSERT_TRUE(value.has_value());
      EXPECT_EQ(*value, static_cast<int>(i));
      EXPECT_EQ(queue.size(), capacity - i - 1);
    }
    EXPECT_FALSE(queue.try_pop().has_value());
    EXPECT_TRUE(queue.empty());
  }
}

TEST(SingleQueue, SQ02FullPushPreservesSourceUntilRetrySucceeds) {
  qlib::single_queue<std::unique_ptr<int>> queue(1);
  ASSERT_TRUE(queue.try_emplace(std::make_unique<int>(11)));
  auto source = std::make_unique<int>(42);
  const auto* address = source.get();
  EXPECT_FALSE(queue.try_push(std::move(source)));
  ASSERT_EQ(source.get(), address);
  EXPECT_EQ(*source, 42);
  const auto first = queue.try_pop();
  ASSERT_TRUE(first && *first);
  EXPECT_EQ(**first, 11);
  ASSERT_TRUE(queue.try_push(std::move(source)));
  EXPECT_EQ(source, nullptr);
  const auto second = queue.try_pop();
  ASSERT_TRUE(second && *second);
  EXPECT_EQ(second->get(), address);
  EXPECT_EQ(**second, 42);
  EXPECT_TRUE(queue.empty());
}

TEST(SingleQueue, SQ01ReservationAndEmptyPopDoNotConstructElements) {
  lifetime_log events;
  {
    qlib::single_queue<tracked> queue(5);
    EXPECT_EQ(queue.capacity(), 8u);
    EXPECT_FALSE(queue.try_pop().has_value());
    EXPECT_EQ(events.attempts, 0u);
    EXPECT_EQ(events.created, 0u);
    EXPECT_EQ(events.destroyed, 0u);
    ASSERT_TRUE(queue.try_emplace(events, 37));
    const auto value = queue.try_pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->value, 37);
    EXPECT_TRUE(queue.empty());
  }
  events.expect_balanced();
}

TEST(SingleQueue, SQ02FullEmplacePreservesMoveOnlyConstructorArgument) {
  qlib::single_queue<std::unique_ptr<int>> queue(1);
  ASSERT_TRUE(queue.try_emplace(std::make_unique<int>(10)));
  auto source = std::make_unique<int>(99);
  const auto* address = source.get();
  EXPECT_FALSE(queue.try_emplace(std::move(source)));
  ASSERT_EQ(source.get(), address);
  EXPECT_EQ(*source, 99);
  {
    const auto first = queue.try_pop();
    ASSERT_TRUE(first && *first);
    EXPECT_EQ(**first, 10);
  }
  ASSERT_TRUE(queue.try_emplace(std::move(source)));
  EXPECT_EQ(source, nullptr);
  const auto result = queue.try_pop();
  ASSERT_TRUE(result && *result);
  EXPECT_EQ(result->get(), address);
  EXPECT_EQ(**result, 99);
}

TEST(SingleQueue, SQ03CapacityOneAlternatesAcrossManySlotWraps) {
  int_queue queue(1);
  for (int expected = 0; expected < 4096; ++expected) {
    ASSERT_TRUE(queue.try_push(int{expected}));
    EXPECT_FALSE(queue.try_emplace(-1));
    EXPECT_EQ(queue.size(), 1u);
    const auto result = queue.try_pop();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, expected);
    EXPECT_FALSE(queue.try_pop().has_value());
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_EQ(queue.capacity(), 1u);
  }
}

TEST(SingleQueue, SQ03BatchFillAndDrainPreservesFifoAcrossSlotWraps) {
  int_queue queue(5);
  for (int batch = 0; batch < 256; ++batch) {
    for (int offset = 0; offset < 8; ++offset) {
      ASSERT_TRUE(queue.try_emplace(batch * 8 + offset));
    }
    EXPECT_FALSE(queue.try_emplace(-1));
    for (int offset = 0; offset < 8; ++offset) {
      const auto result = queue.try_pop();
      ASSERT_TRUE(result.has_value());
      ASSERT_EQ(*result, batch * 8 + offset);
    }
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.capacity(), 8u);
  }
}

TEST(SingleQueue, SQ03PartialDrainAndRefillPreservesFifoAcrossSlotWraps) {
  int_queue queue(4);
  std::vector<int> sent;
  std::vector<int> received;
  sent.reserve(772);
  received.reserve(772);
  for (int value : {10, 20, 30, 40}) {
    ASSERT_TRUE(queue.try_emplace(value));
    sent.push_back(value);
  }
  for (int round = 0; round < 256; ++round) {
    for (int i = 0; i < 3; ++i) {
      const auto result = queue.try_pop();
      ASSERT_TRUE(result.has_value());
      received.push_back(*result);
    }
    EXPECT_EQ(queue.size(), 1u);
    for (int i = 0; i < 3; ++i) {
      const int value = 100 + round * 3 + i;
      ASSERT_TRUE(queue.try_emplace(value));
      sent.push_back(value);
    }
    EXPECT_FALSE(queue.try_emplace(-1));
    EXPECT_EQ(queue.capacity(), 4u);
  }
  while (const auto result = queue.try_pop()) received.push_back(*result);
  EXPECT_EQ(received, sent);
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_TRUE(queue.empty());
}

TEST(SingleQueue, SQ04EmptySlotConstructionFailureDoesNotPublish) {
  lifetime_log events;
  {
    qlib::single_queue<tracked> queue(1);
    EXPECT_THROW(queue.try_emplace(events, 10, true), std::runtime_error);
    EXPECT_EQ(events.attempts, 1u);
    EXPECT_EQ(events.created, 0u);
    EXPECT_EQ(events.destroyed, 0u);
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.try_pop().has_value());
    ASSERT_TRUE(queue.try_emplace(events, 20));
    const auto result = queue.try_pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 20);
  }
  events.expect_balanced();
}

TEST(SingleQueue, SQ04ReusedSlotConstructionFailurePreservesPendingOrder) {
  lifetime_log events;
  {
    qlib::single_queue<tracked> queue(2);
    ASSERT_TRUE(queue.try_emplace(events, 10));
    ASSERT_TRUE(queue.try_emplace(events, 20));
    {
      const auto first = queue.try_pop();
      ASSERT_TRUE(first.has_value());
      EXPECT_EQ(first->value, 10);
    }
    const auto created = events.created;
    const auto destroyed = events.destroyed;
    EXPECT_THROW(queue.try_emplace(events, 30, true), std::runtime_error);
    EXPECT_EQ(events.created, created);
    EXPECT_EQ(events.destroyed, destroyed);
    EXPECT_EQ(queue.size(), 1u);
    ASSERT_TRUE(queue.try_emplace(events, 40));
    for (const int expected : {20, 40}) {
      const auto result = queue.try_pop();
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->value, expected);
    }
    EXPECT_FALSE(queue.try_pop().has_value());
    EXPECT_TRUE(queue.empty());
  }
  events.expect_balanced();
}

TEST(SingleQueue, SQ04FullQueueSkipsThrowingConstructor) {
  lifetime_log events;
  {
    qlib::single_queue<tracked> queue(1);
    ASSERT_TRUE(queue.try_emplace(events, 42));
    const auto attempts = events.attempts;
    const auto created = events.created;
    EXPECT_NO_THROW(EXPECT_FALSE(queue.try_emplace(events, 99, true)));
    EXPECT_EQ(events.attempts, attempts);
    EXPECT_EQ(events.created, created);
    EXPECT_EQ(queue.size(), 1u);
    const auto result = queue.try_pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 42);
  }
  events.expect_balanced();
}

TEST(SingleQueue, SQ05DestructionCleansEmptyPartialAndFullQueues) {
  for (const int remaining : {0, 2, 4}) {
    SCOPED_TRACE(remaining);
    lifetime_log events;
    {
      qlib::single_queue<tracked> queue(4);
      for (int i = 0; i < remaining; ++i) ASSERT_TRUE(queue.try_emplace(events, i));
      EXPECT_EQ(events.alive, static_cast<std::size_t>(remaining));
    }
    EXPECT_EQ(events.created, static_cast<std::size_t>(remaining));
    events.expect_balanced();
  }
}

TEST(SingleQueue, SQ05PopDestroysSlotWhileResultSurvivesReuseAndQueueDestruction) {
  lifetime_log events;
  {
    auto queue = std::make_unique<qlib::single_queue<tracked>>(1);
    ASSERT_TRUE(queue->try_emplace(events, 42));
    const auto original_slot = events.created;
    auto result = queue->try_pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(events.destructions[original_slot], 1u);
    EXPECT_EQ(events.alive, 1u);
    ASSERT_TRUE(queue->try_emplace(events, 99));
    EXPECT_EQ(result->value, 42);
    EXPECT_EQ(events.alive, 2u);
    queue.reset();
    EXPECT_EQ(result->value, 42);
    EXPECT_EQ(events.alive, 1u);
    result.reset();
    EXPECT_EQ(events.alive, 0u);
  }
  events.expect_balanced();
}

TEST(SingleQueue, SQ05DestructionCleansRemainingObjectsAcrossSlotWrap) {
  lifetime_log events;
  {
    qlib::single_queue<tracked> queue(4);
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(queue.try_emplace(events, i));
    for (int expected = 0; expected < 3; ++expected) {
      const auto result = queue.try_pop();
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->value, expected);
    }
    ASSERT_TRUE(queue.try_emplace(events, 4));
    ASSERT_TRUE(queue.try_emplace(events, 5));
    EXPECT_EQ(queue.size(), 3u);
    EXPECT_EQ(events.alive, 3u);
  }
  events.expect_balanced();
}

TEST(SingleQueue, SQ08AcceptsNonThrowingMoveWithoutNoexceptDeclaration) {
  qlib::single_queue<move_without_noexcept> queue(1);
  move_without_noexcept source(17);
  ASSERT_TRUE(queue.try_push(std::move(source)));
  const auto result = queue.try_pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->value, 17);
}

TEST(SingleQueue, SQ09ReusesFixedStorageWithExtendedElementAlignment) {
  std::vector<std::uintptr_t> addresses;
  addresses.reserve(16);
  qlib::single_queue<aligned_value> queue(5);
  for (int cycle = 0; cycle < 2; ++cycle) {
    for (int i = 0; i < 8; ++i) ASSERT_TRUE(queue.try_emplace(addresses, cycle * 8 + i));
    EXPECT_FALSE(queue.try_emplace(addresses, -1));
    for (int i = 0; i < 8; ++i) {
      const auto result = queue.try_pop();
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(result->value, cycle * 8 + i);
    }
    EXPECT_EQ(queue.capacity(), 8u);
  }
  ASSERT_EQ(addresses.size(), 16u);
  for (std::size_t i = 0; i < addresses.size(); ++i) {
    EXPECT_EQ(addresses[i] % alignof(aligned_value), 0u);
    if (i >= 8) {
      EXPECT_EQ(addresses[i], addresses[i - 8]);
    }
  }
}

struct worker_result {
  std::size_t completed = 0;
  std::size_t retries = 0;
  std::size_t created = 0;
  bool stopped = false;
  bool timed_out = false;
  std::string failure;
  std::exception_ptr exception;
};

struct transfer_control {
  std::atomic<bool> stop{false};
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);

  void request_stop() noexcept { stop.store(true, std::memory_order_relaxed); }

  bool keep_running(worker_result& result) {
    if (stop.load(std::memory_order_relaxed)) {
      result.stopped = true;
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      result.timed_out = true;
      request_stop();
      return false;
    }
    return true;
  }
};

class thread_pair {
 public:
  explicit thread_pair(transfer_control& control) : control_(control) {}
  thread_pair(const thread_pair&) = delete;
  thread_pair& operator=(const thread_pair&) = delete;

  ~thread_pair() {
    // 启动异常也先通知已创建的线程停止，再等待线程退出。
    control_.request_stop();
    join();
  }

  template <typename Function>
  void start_producer(worker_result& result, Function function) {
    start(producer_, result, std::move(function));
  }

  template <typename Function>
  void start_consumer(worker_result& result, Function function) {
    start(consumer_, result, std::move(function));
  }

  void join() {
    if (producer_.joinable()) producer_.join();
    if (consumer_.joinable()) consumer_.join();
  }

 private:
  template <typename Function>
  void start(std::jthread& thread, worker_result& result, Function function) {
    thread = std::jthread([this, &result, function = std::move(function)]() mutable {
      try {
        function();
      } catch (...) {
        result.exception = std::current_exception();
        control_.request_stop();
      }
    });
  }

  transfer_control& control_;
  std::jthread producer_;
  std::jthread consumer_;
};

std::string exception_message(const std::exception_ptr& exception) {
  if (!exception) return {};
  try {
    std::rethrow_exception(exception);
  } catch (const std::exception& error) {
    return error.what();
  } catch (...) {
    return "未知异常";
  }
}

void expect_clean_workers(const worker_result& producer, const worker_result& consumer) {
  EXPECT_FALSE(producer.exception) << exception_message(producer.exception);
  EXPECT_FALSE(consumer.exception) << exception_message(consumer.exception);
  EXPECT_FALSE(producer.timed_out);
  EXPECT_FALSE(consumer.timed_out);
  EXPECT_TRUE(producer.failure.empty()) << producer.failure;
  EXPECT_TRUE(consumer.failure.empty()) << consumer.failure;
}

enum class pace { balanced, producer_yields, consumer_yields };

void pause_after_success(pace selected, pace side, std::size_t completed) {
  if (selected == side && completed % 32 == 0) std::this_thread::yield();
}

struct message {
  std::uint64_t sequence;
  std::uint64_t inverse;
  std::uint64_t scaled;
  std::array<std::uint64_t, 4> fields;

  explicit message(std::uint64_t number)
      : sequence(number), inverse(~number), scaled(number * 17 + 3),
        fields{number + 1, number ^ 0xA5A5A5A5A5A5A5A5ULL, number * number,
               number + 0x123456789ABCDEF0ULL} {}
};

void check_message_transfers(std::size_t requested, std::size_t expected_capacity) {
  constexpr std::size_t count = 200000;
  for (const auto selected : {pace::balanced, pace::producer_yields, pace::consumer_yields}) {
    SCOPED_TRACE(static_cast<int>(selected));
    qlib::single_queue<message> queue(requested);
    std::vector<std::uint64_t> sent(count);
    std::vector<std::uint64_t> received(count);
    transfer_control control;
    worker_result producer;
    worker_result consumer;
    thread_pair threads(control);
    threads.start_producer(producer, [&] {
      while (producer.completed < count && control.keep_running(producer)) {
        const auto sequence = producer.completed;
        if (queue.try_emplace(sequence)) {
          sent[sequence] = sequence;
          ++producer.completed;
          pause_after_success(selected, pace::producer_yields, producer.completed);
        } else {
          ++producer.retries;
          std::this_thread::yield();
        }
      }
    });
    threads.start_consumer(consumer, [&] {
      while (consumer.completed < count && control.keep_running(consumer)) {
        if (const auto value = queue.try_pop()) {
          const std::uint64_t expected = consumer.completed;
          if (value->sequence != expected || value->inverse != ~expected ||
              value->scaled != expected * 17 + 3 || value->fields[0] != expected + 1 ||
              value->fields[1] != (expected ^ 0xA5A5A5A5A5A5A5A5ULL) ||
              value->fields[2] != expected * expected ||
              value->fields[3] != expected + 0x123456789ABCDEF0ULL) {
            consumer.failure = "载荷或顺序错误，序号=" + std::to_string(expected);
            control.request_stop();
            return;
          }
          received[consumer.completed++] = value->sequence;
          pause_after_success(selected, pace::consumer_yields, consumer.completed);
        } else {
          ++consumer.retries;
          std::this_thread::yield();
        }
      }
    });
    threads.join();
    expect_clean_workers(producer, consumer);
    EXPECT_FALSE(producer.stopped);
    EXPECT_FALSE(consumer.stopped);
    EXPECT_EQ(producer.completed, count);
    EXPECT_EQ(consumer.completed, count);
    EXPECT_EQ(received, sent);
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.capacity(), expected_capacity);
  }
}

struct counted_resource {
  std::uint64_t sequence;
  std::atomic<std::size_t>* destroyed;

  counted_resource(std::uint64_t number, std::atomic<std::size_t>& counter)
      : sequence(number), destroyed(&counter) {}
  ~counted_resource() { destroyed->fetch_add(1, std::memory_order_relaxed); }
};

void check_owned_transfers(std::size_t requested, std::size_t expected_capacity) {
  constexpr std::size_t count = 50000;
  for (const auto selected : {pace::balanced, pace::producer_yields, pace::consumer_yields}) {
    SCOPED_TRACE(static_cast<int>(selected));
    std::atomic<std::size_t> destroyed{0};
    worker_result producer;
    worker_result consumer;
    std::vector<std::uint64_t> sent(count);
    std::vector<std::uint64_t> received(count);
    {
      qlib::single_queue<std::unique_ptr<counted_resource>> queue(requested);
      transfer_control control;
      thread_pair threads(control);
      threads.start_producer(producer, [&] {
        while (producer.completed < count && control.keep_running(producer)) {
          auto source = std::make_unique<counted_resource>(producer.completed, destroyed);
          ++producer.created;
          const auto* address = source.get();
          while (control.keep_running(producer)) {
            if (queue.try_push(std::move(source))) {
              if (source) {
                producer.failure = "成功入队后源指针仍拥有资源";
                control.request_stop();
                return;
              }
              sent[producer.completed] = producer.completed;
              ++producer.completed;
              pause_after_success(selected, pace::producer_yields, producer.completed);
              break;
            }
            if (source.get() != address) {
              producer.failure = "满失败后源指针丢失资源";
              control.request_stop();
              return;
            }
            ++producer.retries;
            std::this_thread::yield();
          }
        }
      });
      threads.start_consumer(consumer, [&] {
        while (consumer.completed < count && control.keep_running(consumer)) {
          if (const auto value = queue.try_pop()) {
            if (!*value || (*value)->sequence != consumer.completed) {
              consumer.failure = "资源或顺序错误，序号=" + std::to_string(consumer.completed);
              control.request_stop();
              return;
            }
            received[consumer.completed++] = (*value)->sequence;
            pause_after_success(selected, pace::consumer_yields, consumer.completed);
          } else {
            ++consumer.retries;
            std::this_thread::yield();
          }
        }
      });
      threads.join();
      expect_clean_workers(producer, consumer);
      EXPECT_FALSE(producer.stopped);
      EXPECT_FALSE(consumer.stopped);
      EXPECT_EQ(producer.completed, count);
      EXPECT_EQ(consumer.completed, count);
      EXPECT_EQ(received, sent);
      EXPECT_EQ(queue.size(), 0u);
      EXPECT_TRUE(queue.empty());
      EXPECT_EQ(queue.capacity(), expected_capacity);
    }
    EXPECT_EQ(producer.created, count);
    EXPECT_EQ(destroyed.load(std::memory_order_relaxed), producer.created);
  }
}

TEST(SingleQueueConcurrent, SQ06PublishesCompletePayloadAtCapacityOne) {
  check_message_transfers(1, 1);
}

TEST(SingleQueueConcurrent, SQ06PublishesCompletePayloadAtCapacityFour) {
  check_message_transfers(4, 4);
}

TEST(SingleQueueConcurrent, SQ06PublishesCompletePayloadAtRoundedCapacityEight) {
  check_message_transfers(5, 8);
}

TEST(SingleQueueConcurrent, SQ07TransfersUniqueOwnershipAtCapacityOne) {
  check_owned_transfers(1, 1);
}

TEST(SingleQueueConcurrent, SQ07TransfersUniqueOwnershipAtCapacityFour) {
  check_owned_transfers(4, 4);
}

TEST(SingleQueueConcurrent, SQ07TransfersUniqueOwnershipAtRoundedCapacityEight) {
  check_owned_transfers(5, 8);
}

TEST(SingleQueueConcurrent, SQ07ExternalStopJoinsWorkersAndCleansRemainingResources) {
  constexpr std::size_t target = 50000;
  std::atomic<std::size_t> destroyed{0};
  std::atomic<bool> reached_stop_point{false};
  std::atomic<bool> filled_after_pause{false};
  worker_result producer;
  worker_result consumer;
  bool main_timed_out = false;
  {
    qlib::single_queue<std::unique_ptr<counted_resource>> queue(4);
    transfer_control control;
    thread_pair threads(control);
    threads.start_producer(producer, [&] {
      while (producer.completed < target && control.keep_running(producer)) {
        auto source = std::make_unique<counted_resource>(producer.completed, destroyed);
        ++producer.created;
        while (control.keep_running(producer)) {
          if (queue.try_push(std::move(source))) {
            ++producer.completed;
            if (producer.completed == 36) {
              filled_after_pause.store(true, std::memory_order_relaxed);
            }
            break;
          }
          std::this_thread::yield();
        }
      }
    });
    threads.start_consumer(consumer, [&] {
      while (control.keep_running(consumer)) {
        if (consumer.completed == 32) {
          reached_stop_point.store(true, std::memory_order_relaxed);
          std::this_thread::yield();
          continue;
        }
        if (const auto value = queue.try_pop()) {
          if (!*value || (*value)->sequence != consumer.completed) {
            consumer.failure = "停止前的资源交接顺序错误";
            control.request_stop();
            return;
          }
          ++consumer.completed;
        } else {
          std::this_thread::yield();
        }
      }
    });
    while ((!reached_stop_point.load(std::memory_order_relaxed) ||
            !filled_after_pause.load(std::memory_order_relaxed)) &&
           !control.stop.load(std::memory_order_relaxed)) {
      if (std::chrono::steady_clock::now() >= control.deadline) {
        main_timed_out = true;
        break;
      }
      std::this_thread::yield();
    }
    control.request_stop();
    threads.join();
    expect_clean_workers(producer, consumer);
    EXPECT_FALSE(main_timed_out);
    EXPECT_TRUE(reached_stop_point.load(std::memory_order_relaxed));
    EXPECT_TRUE(filled_after_pause.load(std::memory_order_relaxed));
    EXPECT_TRUE(producer.stopped);
    EXPECT_TRUE(consumer.stopped);
    EXPECT_EQ(consumer.completed, 32u);
    EXPECT_EQ(producer.completed, 36u);
    EXPECT_LT(producer.completed, target);
    EXPECT_EQ(queue.size(), producer.completed - consumer.completed);
    EXPECT_EQ(queue.size(), 4u);
    EXPECT_EQ(destroyed.load(std::memory_order_relaxed) + queue.size(), producer.created);
  }
  EXPECT_EQ(destroyed.load(std::memory_order_relaxed), producer.created);
}

struct throwing_message {
  std::size_t sequence;

  throwing_message(std::size_t number, bool fail) : sequence(number) {
    if (fail) throw std::runtime_error("生产者构造失败");
  }
  throwing_message(throwing_message&&) noexcept = default;
};

TEST(SingleQueueConcurrent, SQ04ProducerConstructionExceptionStopsConsumer) {
  qlib::single_queue<throwing_message> queue(4);
  transfer_control control;
  worker_result producer;
  worker_result consumer;
  thread_pair threads(control);
  threads.start_producer(producer, [&] {
    while (producer.completed < 100 && control.keep_running(producer)) {
      if (queue.try_emplace(producer.completed, producer.completed == 32)) ++producer.completed;
      else std::this_thread::yield();
    }
  });
  threads.start_consumer(consumer, [&] {
    while (consumer.completed < 100 && control.keep_running(consumer)) {
      if (const auto value = queue.try_pop()) {
        if (value->sequence != consumer.completed) {
          consumer.failure = "异常前的交接顺序错误";
          control.request_stop();
          return;
        }
        ++consumer.completed;
      } else {
        std::this_thread::yield();
      }
    }
  });
  threads.join();
  ASSERT_TRUE(producer.exception);
  EXPECT_THROW(std::rethrow_exception(producer.exception), std::runtime_error);
  EXPECT_FALSE(consumer.exception) << exception_message(consumer.exception);
  EXPECT_TRUE(producer.failure.empty());
  EXPECT_TRUE(consumer.failure.empty()) << consumer.failure;
  EXPECT_FALSE(producer.timed_out);
  EXPECT_FALSE(consumer.timed_out);
  EXPECT_TRUE(consumer.stopped);
  EXPECT_EQ(producer.completed, 32u);
  EXPECT_LE(consumer.completed, producer.completed);
  EXPECT_EQ(queue.size(), producer.completed - consumer.completed);
}

TEST(SingleQueueConcurrent, SQ07ConsumerExceptionStopsProducer) {
  int_queue queue(4);
  transfer_control control;
  worker_result producer;
  worker_result consumer;
  thread_pair threads(control);
  threads.start_producer(producer, [&] {
    while (producer.completed < 100 && control.keep_running(producer)) {
      if (queue.try_emplace(static_cast<int>(producer.completed))) ++producer.completed;
      else std::this_thread::yield();
    }
  });
  threads.start_consumer(consumer, [&] {
    while (consumer.completed < 100 && control.keep_running(consumer)) {
      if (consumer.completed == 32) throw std::runtime_error("消费者处理失败");
      if (const auto value = queue.try_pop()) {
        if (*value != static_cast<int>(consumer.completed)) {
          consumer.failure = "异常前的交接顺序错误";
          control.request_stop();
          return;
        }
        ++consumer.completed;
      } else {
        std::this_thread::yield();
      }
    }
  });
  threads.join();
  ASSERT_TRUE(consumer.exception);
  EXPECT_THROW(std::rethrow_exception(consumer.exception), std::runtime_error);
  EXPECT_FALSE(producer.exception) << exception_message(producer.exception);
  EXPECT_TRUE(producer.failure.empty());
  EXPECT_TRUE(consumer.failure.empty()) << consumer.failure;
  EXPECT_FALSE(producer.timed_out);
  EXPECT_FALSE(consumer.timed_out);
  EXPECT_TRUE(producer.stopped);
  EXPECT_EQ(consumer.completed, 32u);
  EXPECT_GE(producer.completed, consumer.completed);
  EXPECT_LE(producer.completed, consumer.completed + 4);
  EXPECT_EQ(queue.size(), producer.completed - consumer.completed);
}

TEST(SingleQueueConcurrent, SQ07SecondThreadLaunchFailureJoinsFirstThread) {
  int_queue queue(1);
  transfer_control control;
  worker_result producer;
  bool producer_exited = false;
  bool launch_failed = false;
  try {
    thread_pair threads(control);
    threads.start_producer(producer, [&] {
      while (control.keep_running(producer)) {
        if (queue.try_emplace(42)) ++producer.completed;
        else std::this_thread::yield();
      }
      producer_exited = true;
    });
    // 注入第二线程的启动异常，沿真实作用域退出路径回收首线程。
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
  } catch (const std::system_error&) {
    launch_failed = true;
  }
  EXPECT_TRUE(launch_failed);
  EXPECT_TRUE(producer_exited);
  EXPECT_TRUE(producer.stopped);
  EXPECT_FALSE(producer.timed_out);
  EXPECT_FALSE(producer.exception) << exception_message(producer.exception);
  EXPECT_LE(producer.completed, 1u);
  EXPECT_EQ(queue.size(), producer.completed);
}

using orders_type = qlib::hive<qlib::order>;
using index_type = qlib::map<std::int64_t, qlib::order*>;
using event_queue = qlib::single_queue<qlib::feed_event>;

static_assert(std::is_nothrow_move_constructible_v<qlib::feed_event>);
static_assert(std::is_nothrow_destructible_v<qlib::feed_event>);

enum class event_outcome { added, duplicate, cancelled, missing, mismatch, ignored };

template <typename Record>
Record event_record(std::int64_t seq, qlib::side side) {
  Record record{};
  record.seq = seq;
  record.instrument_id = 1;
  record.side = side;
  record.event_ns = 1000 + seq;
  record.recv_ns = 2000 + seq;
  return record;
}

qlib::feed_event order_event(std::int64_t seq, std::int64_t id, qlib::side side,
                             std::int64_t price, std::int64_t qty) {
  auto record = event_record<qlib::order>(seq, side);
  record.order_id = id;
  record.price = price * qlib::price_scale;
  record.qty = qty * qlib::qty_scale;
  record.new_qty = 0;
  record.prev = nullptr;
  record.next = nullptr;
  record.type = qlib::order_type::limit;
  record.action = qlib::order_action::add;
  return qlib::feed_event{record};
}

qlib::feed_event cancel_event(std::int64_t seq, std::int64_t id, qlib::side side,
                              std::int64_t qty) {
  auto record = event_record<qlib::cancel>(seq, side);
  record.order_id = id;
  record.qty = qty * qlib::qty_scale;
  return qlib::feed_event{record};
}

std::array<qlib::feed_event, 10> fixed_events() {
  auto trade = event_record<qlib::trade>(9, qlib::side::buy);
  trade.buy_order_id = 303;
  trade.sell_order_id = 404;
  trade.price = 99 * qlib::price_scale;
  trade.qty = qlib::qty_scale;
  auto level = event_record<qlib::level>(10, qlib::side::sell);
  level.price = 102 * qlib::price_scale;
  level.qty = 5 * qlib::qty_scale;
  return {order_event(1, 101, qlib::side::buy, 100, 2),
          order_event(2, 202, qlib::side::sell, 101, 3),
          order_event(3, 101, qlib::side::buy, 999, 9),
          cancel_event(4, 999, qlib::side::buy, 1),
          cancel_event(5, 101, qlib::side::buy, 1),
          cancel_event(6, 101, qlib::side::buy, 2),
          order_event(7, 303, qlib::side::buy, 99, 4),
          cancel_event(8, 202, qlib::side::sell, 3),
          qlib::feed_event{trade}, qlib::feed_event{level}};
}

struct expected_event {
  std::int64_t seq;
  std::size_t tag;
  std::int64_t id;
  std::int64_t second_id;
  std::int64_t price;
  std::int64_t qty;
  qlib::side side;
  event_outcome outcome;
  std::vector<std::int64_t> live_ids;
};

// 预期字段和集合逐项填写，独立于事件构造与消费逻辑。
const std::array<expected_event, 10> expected_events{{
    {1, qlib::order_tag, 101, 0, 1'000'000'000'000, 200'000'000, qlib::side::buy,
     event_outcome::added, {101}},
    {2, qlib::order_tag, 202, 0, 1'010'000'000'000, 300'000'000, qlib::side::sell,
     event_outcome::added, {101, 202}},
    {3, qlib::order_tag, 101, 0, 9'990'000'000'000, 900'000'000, qlib::side::buy,
     event_outcome::duplicate, {101, 202}},
    {4, qlib::cancel_tag, 999, 0, 0, 100'000'000, qlib::side::buy,
     event_outcome::missing, {101, 202}},
    {5, qlib::cancel_tag, 101, 0, 0, 100'000'000, qlib::side::buy,
     event_outcome::mismatch, {101, 202}},
    {6, qlib::cancel_tag, 101, 0, 0, 200'000'000, qlib::side::buy,
     event_outcome::cancelled, {202}},
    {7, qlib::order_tag, 303, 0, 990'000'000'000, 400'000'000, qlib::side::buy,
     event_outcome::added, {202, 303}},
    {8, qlib::cancel_tag, 202, 0, 0, 300'000'000, qlib::side::sell,
     event_outcome::cancelled, {303}},
    {9, qlib::trade_tag, 303, 404, 990'000'000'000, 100'000'000, qlib::side::buy,
     event_outcome::ignored, {303}},
    {10, qlib::level_tag, 0, 0, 1'020'000'000'000, 500'000'000, qlib::side::sell,
     event_outcome::ignored, {303}},
}};

void expect_event_fields(const qlib::feed_event& event, const expected_event& expected) {
  ASSERT_EQ(event.index(), expected.tag);
  std::visit([&](const auto& record) {
    using record_type = std::decay_t<decltype(record)>;
    EXPECT_EQ(record.seq, expected.seq);
    EXPECT_EQ(record.instrument_id, 1u);
    EXPECT_EQ(record.side, expected.side);
    EXPECT_EQ(record.qty, expected.qty);
    EXPECT_EQ(record.event_ns, 1000 + expected.seq);
    EXPECT_EQ(record.recv_ns, 2000 + expected.seq);
    if constexpr (!std::is_same_v<record_type, qlib::cancel>) {
      EXPECT_EQ(record.price, expected.price);
    }
    if constexpr (std::is_same_v<record_type, qlib::order> ||
                  std::is_same_v<record_type, qlib::cancel>) {
      EXPECT_EQ(record.order_id, expected.id);
    }
    if constexpr (std::is_same_v<record_type, qlib::order>) {
      EXPECT_EQ(record.type, qlib::order_type::limit);
      EXPECT_EQ(record.action, qlib::order_action::add);
      EXPECT_EQ(record.new_qty, 0);
      EXPECT_EQ(record.prev, nullptr);
      EXPECT_EQ(record.next, nullptr);
    }
    if constexpr (std::is_same_v<record_type, qlib::trade>) {
      EXPECT_EQ(record.buy_order_id, expected.id);
      EXPECT_EQ(record.sell_order_id, expected.second_id);
    }
  }, event);
}

template <typename BeforeIndex>
bool add_indexed(orders_type& orders, index_type& by_id, const qlib::order& input,
                 BeforeIndex before_index) {
  const auto position = orders.emplace(input);
  bool inserted = false;
  try {
    before_index();
    inserted = by_id.try_emplace(std::int64_t{input.order_id},
                                 std::addressof(*position)).second;
  } catch (...) {
    orders.erase(position);
    throw;
  }
  if (!inserted) orders.erase(position);
  return inserted;
}

template <typename BeforeIndex>
event_outcome consume_event(const qlib::feed_event& event, orders_type& orders,
                            index_type& by_id, BeforeIndex before_index) {
  if (const auto* order = std::get_if<qlib::order>(&event)) {
    if (order->action == qlib::order_action::add) {
      return add_indexed(orders, by_id, *order, before_index)
                 ? event_outcome::added : event_outcome::duplicate;
    }
  } else if (const auto* cancel = std::get_if<qlib::cancel>(&event)) {
    const auto found = by_id.find(cancel->order_id);
    if (found == by_id.end()) return event_outcome::missing;
    const auto* order = found->second;
    if (cancel->instrument_id != order->instrument_id || cancel->side != order->side ||
        cancel->qty != order->qty) {
      return event_outcome::mismatch;
    }
    const auto position = orders.get_iterator(found->second);
    by_id.erase(found);
    orders.erase(position);
    return event_outcome::cancelled;
  }
  return event_outcome::ignored;
}

struct stored_order {
  qlib::order value;
  std::uintptr_t address;
};

struct storage_snapshot {
  std::vector<stored_order> orders;
  std::vector<std::int64_t> indexed_ids;
  bool indexes_are_live = true;
};

storage_snapshot observe_storage(const orders_type& orders, const index_type& by_id) {
  storage_snapshot snapshot;
  for (const auto& order : orders) {
    snapshot.orders.push_back({order, reinterpret_cast<std::uintptr_t>(std::addressof(order))});
  }
  for (const auto& [id, pointer] : by_id) {
    snapshot.indexed_ids.push_back(id);
    bool found_live = false;
    for (const auto& order : orders) {
      if (std::addressof(order) == pointer && order.order_id == id) found_live = true;
    }
    snapshot.indexes_are_live = snapshot.indexes_are_live && found_live;
  }
  std::sort(snapshot.orders.begin(), snapshot.orders.end(),
            [](const auto& left, const auto& right) {
    return left.value.order_id < right.value.order_id;
  });
  std::sort(snapshot.indexed_ids.begin(), snapshot.indexed_ids.end());
  return snapshot;
}

void expect_storage(const storage_snapshot& snapshot, const std::vector<std::int64_t>& ids) {
  EXPECT_TRUE(snapshot.indexes_are_live);
  EXPECT_EQ(snapshot.indexed_ids, ids);
  ASSERT_EQ(snapshot.orders.size(), ids.size());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    SCOPED_TRACE(ids[i]);
    EXPECT_EQ(snapshot.orders[i].value.order_id, ids[i]);
    const auto expected_position = ids[i] == 101 ? 0u : ids[i] == 202 ? 1u : 6u;
    expect_event_fields(qlib::feed_event{snapshot.orders[i].value},
                        expected_events[expected_position]);
  }
}

struct event_log {
  std::vector<qlib::feed_event> received;
  std::vector<event_outcome> outcomes;
  std::vector<storage_snapshot> states;
};

template <typename BeforeIndex>
void record_event(const qlib::feed_event& event, orders_type& orders, index_type& by_id,
                  event_log& log, BeforeIndex before_index) {
  const auto position = log.received.size();
  if (position >= expected_events.size() || event.index() != expected_events[position].tag ||
      std::visit([](const auto& record) { return record.seq; }, event) !=
          expected_events[position].seq) {
    throw std::runtime_error("事件类型或序号错误");
  }
  log.received.push_back(event);
  log.outcomes.push_back(consume_event(event, orders, by_id, before_index));
  log.states.push_back(observe_storage(orders, by_id));
}

void expect_event_log(const event_log& log, std::size_t count) {
  ASSERT_EQ(log.received.size(), count);
  ASSERT_EQ(log.outcomes.size(), count);
  ASSERT_EQ(log.states.size(), count);
  for (std::size_t i = 0; i < count; ++i) {
    SCOPED_TRACE(i + 1);
    expect_event_fields(log.received[i], expected_events[i]);
    EXPECT_EQ(log.outcomes[i], expected_events[i].outcome);
    expect_storage(log.states[i], expected_events[i].live_ids);
    if (i == 0) continue;
    for (const auto& current : log.states[i].orders) {
      for (const auto& previous : log.states[i - 1].orders) {
        if (previous.value.order_id == current.value.order_id) {
          EXPECT_EQ(current.address, previous.address);
        }
      }
    }
  }
}

void clear_storage(orders_type& orders, index_type& by_id) {
  const auto count = orders.size();
  by_id.clear();
  EXPECT_TRUE(by_id.empty());
  EXPECT_EQ(orders.size(), count);
  orders.clear();
  EXPECT_TRUE(orders.empty());
}

void produce_events(event_queue& queue, const std::array<qlib::feed_event, 10>& inputs,
                    std::vector<std::int64_t>& sent, transfer_control& control,
                    worker_result& result, std::atomic<bool>& done, pace selected) {
  while (result.completed < inputs.size() && control.keep_running(result)) {
    auto pending = inputs[result.completed];
    while (control.keep_running(result)) {
      if (queue.try_push(std::move(pending))) {
        sent.push_back(std::visit([](const auto& record) { return record.seq; },
                                 inputs[result.completed]));
        ++result.completed;
        if (selected == pace::producer_yields) std::this_thread::yield();
        break;
      }
      ++result.retries;
      std::this_thread::yield();
    }
  }
  done.store(true, std::memory_order_release);
}

template <typename Receive>
void receive_events(event_queue& queue, transfer_control& control, worker_result& result,
                    const std::atomic<bool>& done, pace selected, Receive receive) {
  while (control.keep_running(result)) {
    auto event = queue.try_pop();
    if (!event && done.load(std::memory_order_acquire)) {
      // 完成通知之后重新取值，覆盖首次空观察与最后一次发布之间的交错。
      event = queue.try_pop();
      if (!event) {
        if (result.completed != expected_events.size()) {
          result.failure = "生产者结束时事件尚未收齐";
          control.request_stop();
        }
        return;
      }
    }
    if (event) {
      receive(*event);
      ++result.completed;
      if (selected == pace::consumer_yields) std::this_thread::yield();
    } else {
      ++result.retries;
      std::this_thread::yield();
    }
  }
}

void run_fixed_scenario(bool threaded, pace selected = pace::balanced) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  event_queue queue(2);
  event_log log;
  std::vector<std::int64_t> sent;
  const auto receive = [&](const qlib::feed_event& event) {
    record_event(event, orders, by_id, log, [] {});
  };
  if (threaded) {
    transfer_control control;
    worker_result producer;
    worker_result consumer;
    std::atomic<bool> done{false};
    thread_pair threads(control);
    threads.start_producer(producer, [&] {
      produce_events(queue, inputs, sent, control, producer, done, selected);
    });
    threads.start_consumer(consumer, [&] {
      receive_events(queue, control, consumer, done, selected, receive);
    });
    threads.join();
    expect_clean_workers(producer, consumer);
    EXPECT_FALSE(producer.stopped);
    EXPECT_FALSE(consumer.stopped);
    EXPECT_EQ(producer.completed, 10u);
    EXPECT_EQ(consumer.completed, 10u);
    EXPECT_TRUE(done.load(std::memory_order_acquire));
  } else {
    const auto send = [&](qlib::feed_event& pending) {
      const auto seq = std::visit([](const auto& record) { return record.seq; }, pending);
      if (!queue.try_push(std::move(pending))) return false;
      sent.push_back(seq);
      return true;
    };
    const auto capacity = queue.capacity();
    ASSERT_LT(capacity, inputs.size());
    while (sent.size() < capacity) {
      auto pending = inputs[sent.size()];
      ASSERT_TRUE(send(pending));
    }
    auto pending = inputs[sent.size()];
    ASSERT_FALSE(send(pending));
    EXPECT_EQ(sent.size(), capacity);
    expect_event_fields(pending, expected_events[capacity]);
    auto first = queue.try_pop();
    ASSERT_TRUE(first);
    receive(*first);
    ASSERT_TRUE(send(pending));
    while (sent.size() < inputs.size()) {
      pending = inputs[sent.size()];
      while (!send(pending)) {
        auto event = queue.try_pop();
        ASSERT_TRUE(event);
        receive(*event);
      }
    }
    while (const auto event = queue.try_pop()) receive(*event);
  }
  EXPECT_EQ(sent, (std::vector<std::int64_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
  expect_event_log(log, 10);
  const auto final_state = observe_storage(orders, by_id);
  expect_storage(final_state, {303});
  EXPECT_FALSE(by_id.contains(101));
  EXPECT_FALSE(by_id.contains(202));
  EXPECT_FALSE(by_id.contains(999));
  const auto final = by_id.find(303);
  ASSERT_NE(final, by_id.end());
  ASSERT_TRUE(final_state.indexes_are_live);
  expect_event_fields(qlib::feed_event{*final->second}, expected_events[6]);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0u);
  clear_storage(orders, by_id);
}

TEST(EventStorageIntegration, FixedEventsRetryFullQueueAndPreserveEveryState) {
  run_fixed_scenario(false);
}

TEST(EventStorageIntegration, FixedEventsTransferAcrossTwoThreadsAtDifferentPaces) {
  for (const auto selected : {pace::balanced, pace::producer_yields, pace::consumer_yields}) {
    SCOPED_TRACE(static_cast<int>(selected));
    run_fixed_scenario(true, selected);
  }
}

TEST(EventStorageIntegration, DuplicateKeepsOriginalFieldsAndPointerAndReclaimsCandidate) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  ASSERT_TRUE(add_indexed(orders, by_id, std::get<qlib::order>(inputs[0]), [] {}));
  const auto* original = by_id.at(101);
  std::size_t candidate_count = 0;
  EXPECT_FALSE(add_indexed(orders, by_id, std::get<qlib::order>(inputs[2]), [&] {
    candidate_count = orders.size();
  }));
  EXPECT_EQ(candidate_count, 2u);
  EXPECT_EQ(by_id.at(101), original);
  expect_storage(observe_storage(orders, by_id), {101});
  clear_storage(orders, by_id);
}

struct index_commit_failure : std::runtime_error {
  index_commit_failure() : std::runtime_error("索引提交前失败") {}
};

TEST(EventStorageIntegration, FailureBeforeIndexReclaimsCandidateAndPropagates) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  ASSERT_TRUE(add_indexed(orders, by_id, std::get<qlib::order>(inputs[0]), [] {}));
  const auto* original = by_id.at(101);
  for (const auto position : {1u, 2u}) {
    SCOPED_TRACE(position);
    std::size_t candidate_count = 0;
    EXPECT_THROW(add_indexed(orders, by_id, std::get<qlib::order>(inputs[position]), [&] {
      candidate_count = orders.size();
      throw index_commit_failure{};
    }), index_commit_failure);
    EXPECT_EQ(candidate_count, 2u);
    EXPECT_EQ(by_id.at(101), original);
    expect_storage(observe_storage(orders, by_id), {101});
  }
  EXPECT_FALSE(by_id.contains(202));
  ASSERT_TRUE(add_indexed(orders, by_id, std::get<qlib::order>(inputs[1]), [] {}));
  expect_storage(observe_storage(orders, by_id), {101, 202});
  clear_storage(orders, by_id);
}

TEST(EventStorageIntegration, CancelRequiresMatchingInstrumentSideAndWholeQuantity) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  ASSERT_EQ(consume_event(inputs[0], orders, by_id, [] {}), event_outcome::added);
  ASSERT_EQ(consume_event(inputs[1], orders, by_id, [] {}), event_outcome::added);
  for (int field = 0; field < 3; ++field) {
    auto cancel = std::get<qlib::cancel>(inputs[5]);
    if (field == 0) cancel.instrument_id = 2;
    if (field == 1) cancel.side = qlib::side::sell;
    if (field == 2) cancel.qty = qlib::qty_scale;
    EXPECT_EQ(consume_event(qlib::feed_event{cancel}, orders, by_id, [] {}),
              event_outcome::mismatch);
    expect_storage(observe_storage(orders, by_id), {101, 202});
  }
  auto modify = std::get<qlib::order>(inputs[0]);
  modify.action = qlib::order_action::modify;
  modify.new_qty = 9 * qlib::qty_scale;
  EXPECT_EQ(consume_event(qlib::feed_event{modify}, orders, by_id, [] {}),
            event_outcome::ignored);
  expect_storage(observe_storage(orders, by_id), {101, 202});
  EXPECT_EQ(consume_event(inputs[5], orders, by_id, [] {}), event_outcome::cancelled);
  EXPECT_EQ(consume_event(inputs[5], orders, by_id, [] {}), event_outcome::missing);
  expect_storage(observe_storage(orders, by_id), {202});
  clear_storage(orders, by_id);
}

TEST(EventStorageIntegration, ExternalStopJoinsWorkersWithPendingEvents) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  event_queue queue(2);
  event_log log;
  transfer_control control;
  worker_result producer;
  worker_result consumer;
  std::atomic<bool> consumer_paused{false};
  std::atomic<bool> full_after_pause{false};
  bool main_timed_out = false;
  thread_pair threads(control);
  threads.start_producer(producer, [&] {
    while (producer.completed < inputs.size() && control.keep_running(producer)) {
      auto pending = inputs[producer.completed];
      while (control.keep_running(producer)) {
        if (queue.try_push(std::move(pending))) {
          ++producer.completed;
          break;
        }
        if (producer.completed == 4) full_after_pause.store(true, std::memory_order_relaxed);
        std::this_thread::yield();
      }
    }
  });
  threads.start_consumer(consumer, [&] {
    while (control.keep_running(consumer)) {
      if (consumer.completed == 2) {
        consumer_paused.store(true, std::memory_order_relaxed);
        std::this_thread::yield();
        continue;
      }
      if (const auto event = queue.try_pop()) {
        record_event(*event, orders, by_id, log, [] {});
        ++consumer.completed;
      } else {
        std::this_thread::yield();
      }
    }
  });
  while ((!consumer_paused.load(std::memory_order_relaxed) ||
          !full_after_pause.load(std::memory_order_relaxed)) &&
         !control.stop.load(std::memory_order_relaxed)) {
    if (std::chrono::steady_clock::now() >= control.deadline) {
      main_timed_out = true;
      break;
    }
    std::this_thread::yield();
  }
  control.request_stop();
  threads.join();
  expect_clean_workers(producer, consumer);
  EXPECT_FALSE(main_timed_out);
  EXPECT_TRUE(consumer_paused.load(std::memory_order_relaxed));
  EXPECT_TRUE(full_after_pause.load(std::memory_order_relaxed));
  EXPECT_TRUE(producer.stopped);
  EXPECT_TRUE(consumer.stopped);
  EXPECT_EQ(producer.completed, 4u);
  EXPECT_EQ(consumer.completed, 2u);
  EXPECT_EQ(queue.size(), 2u);
  expect_event_log(log, 2);
  expect_storage(observe_storage(orders, by_id), {101, 202});
  clear_storage(orders, by_id);
}

TEST(EventStorageIntegration, ConsumerCommitExceptionStopsProducerAndPreservesOldOrder) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  event_queue queue(2);
  event_log log;
  transfer_control control;
  worker_result producer;
  worker_result consumer;
  std::vector<std::int64_t> sent;
  std::atomic<bool> done{false};
  thread_pair threads(control);
  threads.start_producer(producer, [&] {
    produce_events(queue, inputs, sent, control, producer, done, pace::balanced);
  });
  threads.start_consumer(consumer, [&] {
    receive_events(queue, control, consumer, done, pace::balanced,
                   [&](const qlib::feed_event& event) {
      record_event(event, orders, by_id, log, [&] {
        if (consumer.completed == 1) throw index_commit_failure{};
      });
    });
  });
  threads.join();
  ASSERT_TRUE(consumer.exception);
  EXPECT_THROW(std::rethrow_exception(consumer.exception), index_commit_failure);
  EXPECT_FALSE(producer.exception) << exception_message(producer.exception);
  EXPECT_FALSE(producer.timed_out);
  EXPECT_FALSE(consumer.timed_out);
  EXPECT_TRUE(producer.failure.empty());
  EXPECT_TRUE(consumer.failure.empty());
  EXPECT_TRUE(producer.stopped);
  EXPECT_EQ(consumer.completed, 1u);
  EXPECT_GE(producer.completed, 2u);
  EXPECT_LE(producer.completed, 4u);
  EXPECT_EQ(queue.size(), producer.completed - 2);
  ASSERT_EQ(log.received.size(), 2u);
  expect_event_fields(log.received[1], expected_events[1]);
  ASSERT_EQ(log.outcomes.size(), 1u);
  EXPECT_EQ(log.outcomes[0], event_outcome::added);
  expect_storage(observe_storage(orders, by_id), {101});
  EXPECT_FALSE(by_id.contains(202));
  clear_storage(orders, by_id);
}

TEST(EventStorageIntegration, SecondThreadLaunchFailureJoinsStartedProducer) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  event_queue queue(2);
  transfer_control control;
  worker_result producer;
  std::vector<std::int64_t> sent;
  std::atomic<bool> done{false};
  bool producer_exited = false;
  bool launch_failed = false;
  try {
    thread_pair threads(control);
    threads.start_producer(producer, [&] {
      produce_events(queue, inputs, sent, control, producer, done, pace::balanced);
      producer_exited = true;
    });
    // 沿第二线程启动失败的作用域退出路径，通知并回收已启动的生产者。
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
  } catch (const std::system_error&) {
    launch_failed = true;
  }
  EXPECT_TRUE(launch_failed);
  EXPECT_TRUE(producer_exited);
  EXPECT_TRUE(producer.stopped);
  EXPECT_FALSE(producer.timed_out);
  EXPECT_FALSE(producer.exception) << exception_message(producer.exception);
  EXPECT_LE(producer.completed, 2u);
  EXPECT_EQ(queue.size(), producer.completed);
  expect_storage(observe_storage(orders, by_id), {});
  clear_storage(orders, by_id);
}

TEST(EventStorageIntegration, ExpiredDeadlineStopsBothWorkersBeforeTransfer) {
  const auto inputs = fixed_events();
  orders_type orders;
  index_type by_id;
  event_queue queue(2);
  transfer_control control{.deadline = std::chrono::steady_clock::now()};
  worker_result producer;
  worker_result consumer;
  std::vector<std::int64_t> sent;
  std::atomic<bool> done{false};
  thread_pair threads(control);
  threads.start_producer(producer, [&] {
    produce_events(queue, inputs, sent, control, producer, done, pace::balanced);
  });
  threads.start_consumer(consumer, [&] {
    receive_events(queue, control, consumer, done, pace::balanced,
                   [&](const qlib::feed_event& event) {
      consume_event(event, orders, by_id, [] {});
    });
  });
  threads.join();
  EXPECT_TRUE(producer.timed_out || consumer.timed_out);
  EXPECT_TRUE(producer.stopped || producer.timed_out);
  EXPECT_TRUE(consumer.stopped || consumer.timed_out);
  EXPECT_FALSE(producer.exception) << exception_message(producer.exception);
  EXPECT_FALSE(consumer.exception) << exception_message(consumer.exception);
  EXPECT_EQ(producer.completed, 0u);
  EXPECT_EQ(consumer.completed, 0u);
  EXPECT_TRUE(queue.empty());
  expect_storage(observe_storage(orders, by_id), {});
  clear_storage(orders, by_id);
}

}  // namespace
