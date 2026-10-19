#include <qlib/pool.h>

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using int_pool = qlib::pool<int>;
static_assert(std::same_as<int_pool::value_type, int>);
static_assert(std::same_as<int_pool::size_type, std::size_t>);
static_assert(!std::is_default_constructible_v<int_pool>);
static_assert(!std::is_copy_constructible_v<int_pool>);
static_assert(!std::is_copy_assignable_v<int_pool>);
static_assert(!std::is_move_constructible_v<int_pool>);
static_assert(!std::is_move_assignable_v<int_pool>);
static_assert(!std::is_convertible_v<std::size_t, int_pool>);
static_assert(std::same_as<decltype(std::declval<int_pool&>()[0]), int&>);
static_assert(std::same_as<decltype(std::declval<const int_pool&>()[0]), const int&>);
static_assert(std::same_as<decltype(std::declval<int_pool&>().emplace(1)), std::size_t>);
static_assert(std::same_as<decltype(std::declval<int_pool&>().release(0)), void>);
static_assert(std::same_as<decltype(std::declval<const int_pool&>().empty()), bool>);
static_assert(std::same_as<decltype(std::declval<const int_pool&>().size()), std::size_t>);
static_assert(std::same_as<decltype(std::declval<const int_pool&>().capacity()), std::size_t>);
static_assert(noexcept(std::declval<int_pool&>()[0]));
static_assert(noexcept(std::declval<const int_pool&>()[0]));
static_assert(noexcept(std::declval<int_pool&>().release(0)));
static_assert(noexcept(std::declval<const int_pool&>().empty()));
static_assert(noexcept(std::declval<const int_pool&>().size()));
static_assert(noexcept(std::declval<const int_pool&>().capacity()));
static_assert(!noexcept(std::declval<int_pool&>().emplace(1)));
static_assert(std::is_nothrow_destructible_v<int_pool>);

struct lifetime_log {
  std::array<int, 8192> destructions{};
  std::size_t created = 0;
  std::size_t moved = 0;
  std::size_t destroyed = 0;
  std::size_t alive = 0;

  std::size_t construct(bool is_move) noexcept {
    ++created;
    ++alive;
    if (is_move) ++moved;
    return created;
  }

  void destroy(std::size_t instance) noexcept {
    EXPECT_GT(instance, 0u);
    EXPECT_LT(instance, destructions.size());
    if (instance > 0 && instance < destructions.size()) {
      EXPECT_EQ(destructions[instance], 0);
      ++destructions[instance];
    }
    ++destroyed;
    --alive;
  }

  void expect_balanced() const {
    EXPECT_EQ(alive, 0u);
    EXPECT_EQ(destroyed, created);
    ASSERT_LT(created, destructions.size());
    for (std::size_t instance = 1; instance <= created; ++instance) {
      EXPECT_EQ(destructions[instance], 1) << instance;
    }
  }
};

struct tracked {
  // 首成员在抛异常前覆盖空槽链接，验证失败分支确实恢复共用存储。
  std::array<std::size_t, 4> payload;
  lifetime_log* log;
  int value;
  std::size_t instance = 0;

  tracked(lifetime_log& events, int number, bool fail = false)
      : payload{0, 0x1234, 0x5678, 0x9ABC}, log(&events), value(number) {
    if (fail) throw std::runtime_error("对象构造失败");
    instance = log->construct(false);
  }

  tracked(const tracked&) = delete;
  tracked& operator=(const tracked&) = delete;
  tracked(tracked&& other) noexcept
      : payload(other.payload), log(other.log), value(std::exchange(other.value, -1)),
        instance(log->construct(true)) {}

  ~tracked() { log->destroy(instance); }
};

static_assert(offsetof(tracked, payload) == 0);
static_assert(!std::is_default_constructible_v<tracked>);
static_assert(!std::is_copy_constructible_v<tracked>);
static_assert(std::is_nothrow_move_constructible_v<tracked>);

struct move_only {
  std::unique_ptr<int> resource;
  std::string name;

  move_only(std::unique_ptr<int> value, std::string label)
      : resource(std::move(value)), name(std::move(label)) {}
  move_only(move_only&&) noexcept = default;
};

struct alignas(64) aligned_value {
  int value;

  explicit aligned_value(int number) : value(number) {}
  aligned_value(aligned_value&&) noexcept = default;
};

struct move_without_noexcept {
  int value;

  explicit move_without_noexcept(int number) : value(number) {}
  move_without_noexcept(const move_without_noexcept&) = delete;
  move_without_noexcept(move_without_noexcept&& other) : value(other.value) {}
};

static_assert(!std::is_nothrow_move_constructible_v<move_without_noexcept>);

TEST(PoolOBJ01, ReservationDoesNotConstructObjects) {
  for (const std::size_t requested : {0u, 1u, 32u}) {
    lifetime_log events;
    {
      qlib::pool<tracked> values(requested);
      EXPECT_TRUE(values.empty());
      EXPECT_EQ(values.size(), 0u);
      EXPECT_GE(values.capacity(), requested);
      EXPECT_EQ(events.created, 0u);
      EXPECT_EQ(events.alive, 0u);
    }
    events.expect_balanced();
  }
}

TEST(PoolOBJ01, ZeroReservationCanCreateAndRelease) {
  int_pool values(0);
  const auto handle = values.emplace(42);
  EXPECT_EQ(handle, 0u);
  EXPECT_EQ(values[handle], 42);
  EXPECT_EQ(values.size(), 1u);
  EXPECT_FALSE(values.empty());
  const auto capacity = values.capacity();
  EXPECT_GE(capacity, 1u);
  values.release(handle);
  EXPECT_TRUE(values.empty());
  EXPECT_EQ(values.size(), 0u);
  EXPECT_EQ(values.capacity(), capacity);
}

TEST(PoolOBJ02, ForwardsArgumentsAndProvidesMutableAndConstAccess) {
  qlib::pool<move_only> values(1);
  auto resource = std::make_unique<int>(17);
  const auto handle = values.emplace(std::move(resource), "original");
  EXPECT_EQ(resource, nullptr);
  ASSERT_NE(values[handle].resource, nullptr);
  EXPECT_EQ(*values[handle].resource, 17);
  EXPECT_EQ(values[handle].name, "original");
  *values[handle].resource = 29;
  values[handle].name = "updated";
  const auto& read_only = values;
  EXPECT_EQ(*read_only[handle].resource, 29);
  EXPECT_EQ(read_only[handle].name, "updated");
  EXPECT_EQ(read_only.size(), 1u);
}

TEST(PoolOBJ03, ReusesMultipleSlotsInReverseReleaseOrder) {
  int_pool values(8);
  std::array<std::size_t, 5> handles;
  for (std::size_t i = 0; i < handles.size(); ++i) {
    handles[i] = values.emplace(static_cast<int>(10 + i));
  }
  const auto capacity = values.capacity();
  values.release(handles[0]);
  values.release(handles[2]);
  values.release(handles[4]);
  EXPECT_EQ(values.size(), 2u);
  EXPECT_EQ(values.capacity(), capacity);
  const auto first = values.emplace(40);
  const auto second = values.emplace(20);
  const auto third = values.emplace(0);
  EXPECT_EQ(first, handles[4]);
  EXPECT_EQ(second, handles[2]);
  EXPECT_EQ(third, handles[0]);
  EXPECT_EQ(values[first], 40);
  EXPECT_EQ(values[second], 20);
  EXPECT_EQ(values[third], 0);
  EXPECT_EQ(values[handles[1]], 11);
  EXPECT_EQ(values[handles[3]], 13);
  EXPECT_EQ(values.size(), 5u);
  EXPECT_EQ(values.capacity(), capacity);
}

TEST(PoolOBJ03, InterleavesReleaseAndReuseThenReusesAnEmptyPool) {
  lifetime_log events;
  {
    qlib::pool<tracked> values(4);
    const auto first = values.emplace(events, 10);
    const auto middle = values.emplace(events, 20);
    const auto last = values.emplace(events, 30);
    const auto capacity = values.capacity();
    values.release(middle);
    EXPECT_EQ(events.destroyed, 1u);
    const auto replacement = values.emplace(events, 40);
    EXPECT_EQ(replacement, middle);
    EXPECT_EQ(values[replacement].value, 40);
    values.release(first);
    values.release(last);
    values.release(replacement);
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(events.alive, 0u);
    EXPECT_EQ(values.capacity(), capacity);
    const auto reused = values.emplace(events, 50);
    EXPECT_EQ(reused, replacement);
    EXPECT_EQ(values[reused].value, 50);
    EXPECT_EQ(events.alive, 1u);
    EXPECT_EQ(values.size(), 1u);
  }
  events.expect_balanced();
}

TEST(PoolOBJ04, HandlesKeepMoveOnlyResourcesAcrossRealGrowth) {
  qlib::pool<std::unique_ptr<int>> values(1);
  std::vector<std::size_t> handles;
  for (int round = 0; round < 4; ++round) {
    const auto capacity = values.capacity();
    while (values.size() <= capacity) {
      const int expected = static_cast<int>(100 + handles.size());
      handles.push_back(values.emplace(std::make_unique<int>(expected)));
    }
    EXPECT_GT(values.capacity(), capacity);
    for (std::size_t i = 0; i < handles.size(); ++i) {
      ASSERT_NE(values[handles[i]], nullptr);
      EXPECT_EQ(*values[handles[i]], static_cast<int>(100 + i));
    }
  }
}

TEST(PoolOBJ04, PreservesOverAlignedValuesAcrossGrowthAndReuse) {
  qlib::pool<aligned_value> values(1);
  std::vector<std::size_t> handles;
  const auto capacity = values.capacity();
  for (std::size_t i = 0; i <= capacity; ++i) {
    handles.push_back(values.emplace(static_cast<int>(70 + i)));
  }
  EXPECT_GT(values.capacity(), capacity);
  for (std::size_t i = 0; i < handles.size(); ++i) {
    const auto& value = values[handles[i]];
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(std::addressof(value)) % alignof(aligned_value),
              0u);
    EXPECT_EQ(value.value, static_cast<int>(70 + i));
  }
  values.release(handles.front());
  const auto reused = values.emplace(99);
  EXPECT_EQ(reused, handles.front());
  EXPECT_EQ(values[reused].value, 99);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(std::addressof(values[reused])) %
                alignof(aligned_value),
            0u);
}

TEST(PoolOBJ04, SupportsNonthrowingMoveWithoutNoexceptDeclaration) {
  qlib::pool<move_without_noexcept> values(1);
  const auto first = values.emplace(37);
  const auto capacity = values.capacity();
  while (values.size() <= capacity) values.emplace(81);
  EXPECT_GT(values.capacity(), capacity);
  EXPECT_EQ(values[first].value, 37);
}

TEST(PoolOBJ04, SavesConstructionArgumentsOutsidePoolBeforeGrowth) {
  qlib::pool<std::string> values(1);
  const auto first = values.emplace("saved outside pool");
  const auto capacity = values.capacity();
  while (values.size() < capacity) values.emplace("filler");
  const std::string saved = values[first];
  const auto appended = values.emplace(saved);
  EXPECT_GT(values.capacity(), capacity);
  EXPECT_EQ(values[first], "saved outside pool");
  EXPECT_EQ(values[appended], "saved outside pool");
}

TEST(PoolOBJ05, DestroysEveryMovedPhysicalInstanceExactlyOnce) {
  lifetime_log events;
  {
    qlib::pool<tracked> values(1);
    std::vector<std::size_t> handles;
    const auto capacity = values.capacity();
    for (std::size_t i = 0; i < capacity; ++i) {
      handles.push_back(values.emplace(events, static_cast<int>(100 + i)));
    }
    std::vector<std::size_t> original_instances;
    for (const auto handle : handles) original_instances.push_back(values[handle].instance);
    EXPECT_EQ(events.moved, 0u);
    EXPECT_EQ(events.destroyed, 0u);
    handles.push_back(values.emplace(events, 999));
    EXPECT_GT(values.capacity(), capacity);
    EXPECT_EQ(events.moved, capacity);
    EXPECT_EQ(events.destroyed, capacity);
    EXPECT_EQ(events.alive, values.size());
    for (std::size_t i = 0; i < original_instances.size(); ++i) {
      EXPECT_EQ(events.destructions[original_instances[i]], 1);
      EXPECT_NE(values[handles[i]].instance, original_instances[i]);
      EXPECT_EQ(values[handles[i]].value, static_cast<int>(100 + i));
    }
    const auto released_instance = values[handles.front()].instance;
    values.release(handles.front());
    EXPECT_EQ(events.destructions[released_instance], 1);
    EXPECT_EQ(events.alive, values.size());
  }
  events.expect_balanced();
}

TEST(PoolOBJ05, PoolDestructionOnlyDestroysRemainingLiveObjects) {
  lifetime_log events;
  {
    qlib::pool<tracked> values(8);
    const auto first = values.emplace(events, 10);
    const auto second = values.emplace(events, 20);
    values.emplace(events, 30);
    const auto instance = values[second].instance;
    values.release(second);
    EXPECT_EQ(events.destructions[instance], 1);
    EXPECT_EQ(events.destroyed, 1u);
    EXPECT_EQ(events.alive, 2u);
    EXPECT_EQ(values[first].value, 10);
  }
  EXPECT_EQ(events.created, 3u);
  events.expect_balanced();
}

TEST(PoolExceptions, ReuseFailureRestoresOverwrittenHeadAndSuccessor) {
  lifetime_log events;
  {
    qlib::pool<tracked> values(4);
    const auto survivor = values.emplace(events, 10);
    const auto first_free = values.emplace(events, 20);
    const auto last_free = values.emplace(events, 30);
    values.release(first_free);
    values.release(last_free);
    const auto capacity = values.capacity();
    const auto created = events.created;
    const auto destroyed = events.destroyed;
    EXPECT_THROW(values.emplace(events, 99, true), std::runtime_error);
    EXPECT_THROW(values.emplace(events, 98, true), std::runtime_error);
    EXPECT_EQ(values.size(), 1u);
    EXPECT_EQ(values.capacity(), capacity);
    EXPECT_EQ(values[survivor].value, 10);
    EXPECT_EQ(events.created, created);
    EXPECT_EQ(events.destroyed, destroyed);
    EXPECT_EQ(events.alive, 1u);
    const auto first = values.emplace(events, 31);
    ASSERT_EQ(first, last_free);
    const auto second = values.emplace(events, 21);
    ASSERT_EQ(second, first_free);
    EXPECT_EQ(values[first].value, 31);
    EXPECT_EQ(values[second].value, 21);
    EXPECT_EQ(values[survivor].value, 10);
    const auto appended = values.emplace(events, 40);
    EXPECT_EQ(appended, 3u);
    EXPECT_EQ(values.size(), 4u);
    values.release(first);
    values.release(second);
    values.release(appended);
    values.release(survivor);
    EXPECT_TRUE(values.empty());
  }
  events.expect_balanced();
}

TEST(PoolExceptions, AppendFailureWithinReservationRemovesTemporarySlot) {
  lifetime_log events;
  {
    qlib::pool<tracked> values(4);
    const auto first = values.emplace(events, 10);
    const auto capacity = values.capacity();
    EXPECT_THROW(values.emplace(events, 99, true), std::runtime_error);
    EXPECT_EQ(values.size(), 1u);
    EXPECT_EQ(values.capacity(), capacity);
    EXPECT_EQ(values[first].value, 10);
    EXPECT_EQ(events.created, 1u);
    EXPECT_EQ(events.destroyed, 0u);
    EXPECT_EQ(events.alive, 1u);
    const auto appended = values.emplace(events, 20);
    EXPECT_EQ(appended, 1u);
    EXPECT_EQ(values[appended].value, 20);
    values.release(appended);
    values.release(first);
    EXPECT_TRUE(values.empty());
  }
  events.expect_balanced();
}

TEST(PoolExceptions, AppendFailureAfterGrowthPreservesHandlesAndNewCapacity) {
  lifetime_log events;
  {
    qlib::pool<tracked> values(1);
    std::vector<std::size_t> handles;
    const auto capacity = values.capacity();
    for (std::size_t i = 0; i < capacity; ++i) {
      handles.push_back(values.emplace(events, static_cast<int>(40 + i)));
    }
    EXPECT_THROW(values.emplace(events, 99, true), std::runtime_error);
    EXPECT_GT(values.capacity(), capacity);
    EXPECT_EQ(values.size(), capacity);
    EXPECT_EQ(events.created, 2 * capacity);
    EXPECT_EQ(events.moved, capacity);
    EXPECT_EQ(events.destroyed, capacity);
    EXPECT_EQ(events.alive, capacity);
    for (std::size_t i = 0; i < handles.size(); ++i) {
      EXPECT_EQ(values[handles[i]].value, static_cast<int>(40 + i));
    }
    const auto appended = values.emplace(events, 100);
    EXPECT_EQ(appended, capacity);
    EXPECT_EQ(values[appended].value, 100);
    values.release(appended);
    for (const auto handle : handles) values.release(handle);
    EXPECT_TRUE(values.empty());
  }
  events.expect_balanced();
}

}  // namespace
