#include <qlib/single_queue.h>

#include <gtest/gtest.h>

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

}  // namespace
