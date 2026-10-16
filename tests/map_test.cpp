#include <qlib/map.h>
#include <qlib/common.h>
#include <qlib/hive.h>

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using int_map = qlib::map<int, int>;

template <typename M>
concept accepts_lvalue_insert = requires(M& m, typename M::value_type& v) { m.insert(v); };
template <typename M>
concept accepts_lvalue_key = requires(M& m, typename M::key_type& k) { m.try_emplace(k); };
template <typename M>
concept accepts_lvalue_subscript = requires(M& m, typename M::key_type& k) { m[k]; };

static_assert(std::is_same_v<int_map::key_type, int>);
static_assert(std::is_same_v<int_map::mapped_type, int>);
static_assert(std::is_same_v<int_map::value_type, std::pair<int, int>>);
static_assert(std::is_same_v<int_map::size_type, std::size_t>);
static_assert(std::is_same_v<int_map::difference_type, std::ptrdiff_t>);
static_assert(std::is_same_v<int_map::hasher, std::hash<int>>);
static_assert(std::is_same_v<int_map::key_equal, std::equal_to<int>>);
static_assert(std::is_same_v<int_map::reference, int_map::value_type&>);
static_assert(std::is_same_v<int_map::const_reference, const int_map::value_type&>);
static_assert(std::is_same_v<int_map::pointer, int_map::value_type*>);
static_assert(std::is_same_v<int_map::const_pointer, const int_map::value_type*>);
static_assert(std::forward_iterator<int_map::iterator>);
static_assert(std::forward_iterator<int_map::const_iterator>);
static_assert(std::is_convertible_v<int_map::iterator, int_map::const_iterator>);
static_assert(!std::is_convertible_v<int_map::const_iterator, int_map::iterator>);
static_assert(std::is_same_v<decltype(*std::declval<int_map::const_iterator>()),
                             int_map::const_reference>);
static_assert(std::is_same_v<decltype(std::declval<const int_map&>().at(0)), const int&>);
static_assert(!std::is_copy_constructible_v<int_map> && !std::is_copy_assignable_v<int_map>);
static_assert(std::is_nothrow_default_constructible_v<int_map>);
static_assert(std::is_nothrow_move_constructible_v<int_map>);
static_assert(std::is_nothrow_move_assignable_v<int_map>);
static_assert(std::is_nothrow_destructible_v<int_map>);
static_assert(!accepts_lvalue_insert<int_map> && !accepts_lvalue_key<int_map>);
static_assert(!accepts_lvalue_subscript<int_map>);
static_assert(noexcept(std::declval<int_map&>().begin()));
static_assert(noexcept(std::declval<const int_map&>().begin()));
static_assert(noexcept(std::declval<int_map&>().end()));
static_assert(noexcept(std::declval<const int_map&>().end()));
static_assert(noexcept(std::declval<const int_map&>().cbegin()));
static_assert(noexcept(std::declval<const int_map&>().cend()));
static_assert(noexcept(std::declval<const int_map&>().empty()));
static_assert(noexcept(std::declval<const int_map&>().size()));
static_assert(noexcept(std::declval<const int_map&>().capacity()));
static_assert(noexcept(std::declval<const int_map&>().max_size()));
static_assert(noexcept(std::declval<int_map&>().find(0)));
static_assert(noexcept(std::declval<const int_map&>().find(0)));
static_assert(noexcept(std::declval<const int_map&>().contains(0)));
static_assert(noexcept(std::declval<int_map&>().erase(0)));
static_assert(noexcept(std::declval<int_map&>().erase(std::declval<int_map::const_iterator>())));
static_assert(noexcept(std::declval<int_map&>().clear()));
static_assert(noexcept(std::declval<int_map&>().swap(std::declval<int_map&>())));
static_assert(noexcept(qlib::swap(std::declval<int_map&>(), std::declval<int_map&>())));
static_assert(noexcept(*std::declval<int_map::iterator>()));
static_assert(noexcept(std::declval<int_map::iterator>().operator->()));
static_assert(noexcept(++std::declval<int_map::iterator&>()));
static_assert(noexcept(std::declval<int_map::iterator&>()++));
static_assert(noexcept(std::declval<int_map::iterator>() == std::declval<int_map::iterator>()));

// 哈希值 3 在八槽表从槽 6 开始，让六个冲突键跨过数组边界。
struct collision_hash {
  std::size_t operator()(int) const noexcept { return 3; }
};

// 额外键从槽 4 开始，能在 used 已满时命中原空槽。
struct split_hash {
  std::size_t operator()(int key) const noexcept { return key < 100 ? 3 : 1; }
};

struct tracked {
  inline static int live = 0;
  inline static int constructed = 0;
  inline static int moved = 0;
  inline static int destroyed = 0;
  inline static bool fail = false;
  int value;

  explicit tracked(int v) : value(v) {
    if (fail) throw std::runtime_error("新值构造失败");
    ++live;
    ++constructed;
  }
  tracked(const tracked&) = delete;
  tracked& operator=(const tracked&) = delete;
  tracked(tracked&& other) noexcept : value(std::exchange(other.value, -1)) {
    ++live;
    ++constructed;
    ++moved;
  }
  ~tracked() noexcept { --live; ++destroyed; }
  static void reset() {
    live = constructed = moved = destroyed = 0;
    fail = false;
  }
};

class map_lifetime : public testing::Test {
 protected:
  void SetUp() override { tracked::reset(); }
  void TearDown() override {
    EXPECT_EQ(tracked::live, 0);
    EXPECT_EQ(tracked::constructed, tracked::destroyed);
    tracked::fail = false;
  }
};

struct move_key {
  int value;
  explicit move_key(int v) noexcept : value(v) {}
  move_key(const move_key&) = delete;
  move_key& operator=(const move_key&) = delete;
  move_key(move_key&& other) noexcept : value(std::exchange(other.value, -1)) {}
};
struct move_key_hash {
  std::size_t operator()(const move_key& key) const noexcept { return key.value; }
};
struct move_key_equal {
  bool operator()(const move_key& a, const move_key& b) const noexcept {
    return a.value == b.value;
  }
};

struct alignas(256) aligned_value {
  int value;
  explicit aligned_value(int v) noexcept : value(v) {}
  aligned_value(aligned_value&&) noexcept = default;
  aligned_value(const aligned_value&) = delete;
};

struct policy_config {
  inline static int modulus = 10;
  inline static int salt = 7;
};
struct stateful_hash {
  int modulus = policy_config::modulus;
  int salt = policy_config::salt;
  std::size_t operator()(int key) const noexcept { return key % modulus + salt; }
};
struct stateful_equal {
  int modulus = policy_config::modulus;
  bool operator()(int a, int b) const noexcept { return a % modulus == b % modulus; }
};

// MP-01/02：空态查询不能隐式分配，也不能把键 0 当作空标记。
TEST(Map, EmptyQueriesAndZeroKey) {
  int_map map;
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.size(), 0u);
  EXPECT_EQ(map.capacity(), 0u);
  EXPECT_EQ(map.max_size(), std::size_t(-1) / sizeof(int_map::value_type));
  EXPECT_EQ(map.begin(), map.end());
  EXPECT_EQ(map.cbegin(), map.cend());
  EXPECT_EQ(map.find(0), map.end());
  EXPECT_FALSE(map.contains(0));
  EXPECT_EQ(map.erase(0), 0u);
  EXPECT_THROW(map.at(0), std::out_of_range);
  const auto& readonly = map;
  EXPECT_EQ(readonly.find(0), readonly.end());
  EXPECT_THROW(readonly.at(0), std::out_of_range);
  EXPECT_EQ(map.capacity(), 0u);
  auto [it, inserted] = map.insert({0, 42});
  EXPECT_TRUE(inserted);
  EXPECT_EQ(it->second, 42);
  EXPECT_EQ(map.capacity(), 8u);
}

TEST(Map, InsertEmplaceTryEmplaceAndUpdates) {
  qlib::map<int, std::string> map;
  EXPECT_TRUE(map.insert({1, "one"}).second);
  EXPECT_TRUE(map.emplace(2, "two").second);
  EXPECT_TRUE(map.try_emplace(3, 5, 'x').second);
  EXPECT_EQ(map.at(3), "xxxxx");
  EXPECT_FALSE(map.insert({1, "replacement"}).second);
  EXPECT_FALSE(map.emplace(2, "replacement").second);
  EXPECT_FALSE(map.try_emplace(3, "replacement").second);
  EXPECT_EQ(map.at(1), "one");
  EXPECT_EQ(map.at(2), "two");
  map.at(1) = "changed";
  map.find(2)->second = "updated";
  EXPECT_EQ(map.at(1), "changed");
  EXPECT_EQ(map.at(2), "updated");
  EXPECT_EQ(map.size(), 3u);
}

TEST(Map, SubscriptValueInitializesMissingAndReturnsExistingValue) {
  int_map map;
  EXPECT_EQ(map[1], 0);
  map[1] = 19;
  EXPECT_EQ(map[1], 19);
  EXPECT_EQ(map.size(), 1u);
}

TEST(Map, DuplicateInsertDoesNotMoveKeyOrValue) {
  qlib::map<std::string, std::unique_ptr<int>> map;
  const std::string key(160, 'k');
  map.try_emplace(std::string(key), std::make_unique<int>(3));
  decltype(map)::value_type value{key, std::make_unique<int>(8)};
  const auto* pointer = value.second.get();
  auto [it, inserted] = map.insert(std::move(value));
  EXPECT_FALSE(inserted);
  EXPECT_EQ(value.first, key);
  EXPECT_EQ(value.second.get(), pointer);
  EXPECT_EQ(*it->second, 3);
}

TEST(Map, DuplicateTryEmplaceDoesNotMoveArguments) {
  qlib::map<std::string, std::unique_ptr<int>> map;
  std::string key(160, 'k');
  map.try_emplace(std::string(key), std::make_unique<int>(3));
  auto value = std::make_unique<int>(8);
  const auto* pointer = value.get();
  EXPECT_FALSE(map.try_emplace(std::move(key), std::move(value)).second);
  EXPECT_EQ(key, std::string(160, 'k'));
  EXPECT_EQ(value.get(), pointer);
  EXPECT_EQ(*map.at(key), 3);
}

TEST_F(map_lifetime, DuplicateEmplaceConstructsAndDestroysTemporary) {
  qlib::map<int, tracked> map;
  map.try_emplace(1, 10);
  const int before = tracked::constructed;
  EXPECT_FALSE(map.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                           std::forward_as_tuple(20)).second);
  EXPECT_EQ(tracked::constructed, before + 1);
  EXPECT_EQ(tracked::destroyed, 1);
  EXPECT_EQ(tracked::live, 1);
  EXPECT_EQ(map.at(1).value, 10);
}

TEST(Map, ConstQueriesAndMissingAtPreserveSize) {
  int_map map;
  map.try_emplace(7, 70);
  const auto& readonly = map;
  EXPECT_EQ(readonly.at(7), 70);
  ASSERT_NE(readonly.find(7), readonly.end());
  EXPECT_EQ(readonly.find(7)->second, 70);
  EXPECT_TRUE(readonly.contains(7));
  EXPECT_FALSE(readonly.contains(8));
  EXPECT_EQ(readonly.find(8), readonly.end());
  for (bool read_only : {false, true}) {
    try {
      if (read_only) (void)readonly.at(8);
      else (void)map.at(8);
      FAIL() << "缺键必须抛出";
    } catch (const std::out_of_range& error) {
      EXPECT_STREQ(error.what(), "qlib::map::at");
    }
  }
  EXPECT_EQ(map.size(), 1u);
}

// MP-03：查重必须跨过墓碑；缺键才允许复用第一个墓碑。
TEST(Map, CollisionWraparoundAndDuplicateBeyondTombstone) {
  qlib::map<int, int, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key * 10);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(map.erase(0), 1u);
  EXPECT_EQ(map.erase(2), 1u);
  for (int key : {1, 3, 4, 5}) EXPECT_EQ(map.at(key), key * 10);
  const auto result = map.try_emplace(5, 999);
  EXPECT_FALSE(result.second);
  EXPECT_EQ(result.first->second, 50);
  EXPECT_EQ(map.size(), 4u);
  EXPECT_FALSE(map.contains(999));
}

TEST_F(map_lifetime, FirstTombstoneIsReusedWithoutGrowthOrMigration) {
  qlib::map<int, tracked, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  const auto first = reinterpret_cast<std::uintptr_t>(&*map.find(0));
  map.erase(0);
  map.erase(2);
  const int moved = tracked::moved;
  auto result = map.try_emplace(9, 90);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&*result.first), first);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, moved);
  EXPECT_EQ(map.size(), 5u);
  EXPECT_EQ(map.at(5).value, 5);
}

TEST_F(map_lifetime, EmptyNeighborReturnsCapacityBudget) {
  qlib::map<int, tracked, split_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  map.erase(5);
  const int moved = tracked::moved;
  EXPECT_TRUE(map.try_emplace(100, 100).second);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, moved);
  EXPECT_EQ(map.size(), 6u);
}

TEST_F(map_lifetime, SparseUsedBudgetTriggersSameCapacityCleanup) {
  qlib::map<int, tracked, split_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  for (int key = 0; key < 4; ++key) map.erase(key);
  const int moved = tracked::moved;
  map.try_emplace(100, 100);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, moved + 2);
  EXPECT_EQ(map.size(), 3u);
  EXPECT_EQ(map.at(4).value, 4);
  EXPECT_EQ(map.at(5).value, 5);
  EXPECT_EQ(map.at(100).value, 100);
}

TEST_F(map_lifetime, HalfFullUsedBudgetTriggersGrowth) {
  qlib::map<int, tracked, split_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  map.erase(0);
  map.erase(1);
  const int moved = tracked::moved;
  map.try_emplace(100, 100);
  EXPECT_EQ(map.capacity(), 16u);
  EXPECT_EQ(tracked::moved, moved + 4);
  EXPECT_EQ(map.size(), 5u);
  for (int key = 2; key < 6; ++key) EXPECT_EQ(map.at(key).value, key);
}

// MP-04：reserve 的总量与墓碑预算都影响下一次是否搬迁。
TEST(Map, ReserveZeroAndBoundaryCapacities) {
  for (const auto& [total, expected_capacity] :
       std::vector<std::pair<std::size_t, std::size_t>>{{0, 0}, {1, 8}, {6, 8},
                                                       {7, 16}, {12, 16}, {13, 32}}) {
    int_map map;
    map.reserve(total);
    EXPECT_EQ(map.capacity(), expected_capacity);
    EXPECT_EQ(map.begin(), map.end());
    for (std::size_t key = 0; key < total; ++key) {
      map.try_emplace(int(key), int(key));
      EXPECT_EQ(map.capacity(), expected_capacity);
    }
    map.reserve(0);
    EXPECT_EQ(map.size(), total);
    EXPECT_EQ(map.capacity(), expected_capacity);
  }
}

TEST_F(map_lifetime, ReserveIncludesTombstonesAndThenPreventsMigration) {
  qlib::map<int, tracked, split_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  for (int key = 0; key < 4; ++key) map.erase(key);
  const int before = tracked::moved;
  map.reserve(6);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, before + 2);
  const int reserved = tracked::moved;
  for (int key = 100; key < 104; ++key) map.try_emplace(int(key), key);
  EXPECT_EQ(map.size(), 6u);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, reserved);
  EXPECT_EQ(map.at(4).value, 4);
  EXPECT_EQ(map.at(5).value, 5);
}

TEST_F(map_lifetime, ReserveWithinTombstoneBudgetDoesNotMigrate) {
  qlib::map<int, tracked, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  map.erase(0);
  const int moved = tracked::moved;
  map.reserve(5);
  EXPECT_EQ(tracked::moved, moved);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(map.at(5).value, 5);
}

TEST(Map, RepeatedGrowthPreservesAllValues) {
  int_map map;
  for (int key = 0; key < 4096; ++key) map.try_emplace(int(key), -key);
  EXPECT_EQ(map.size(), 4096u);
  for (int key = 0; key < 4096; ++key) EXPECT_EQ(map.at(key), -key);
}

TEST(Map, IteratorConversionIncrementAndConstTraversal) {
  qlib::map<int, int, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key * 10);
  map.erase(0);
  map.erase(3);
  int_map::iterator default_a;
  int_map::iterator default_b;
  EXPECT_EQ(default_a, default_b);
  auto it = map.begin();
  decltype(map)::const_iterator readonly = it;
  EXPECT_EQ(it, readonly);
  EXPECT_EQ(readonly, it);
  EXPECT_EQ(&*it, it.operator->());
  auto old = it++;
  EXPECT_EQ(old, readonly);
  auto* self = &it;
  EXPECT_EQ(&++it, self);
  std::set<int> seen;
  for (auto cursor = map.begin(); cursor != map.end(); ++cursor) {
    EXPECT_TRUE(seen.insert(cursor->first).second);
    cursor->second += 1;
  }
  EXPECT_EQ(seen, (std::set<int>{1, 2, 4, 5}));
  const auto& const_map = map;
  seen.clear();
  for (auto cursor = const_map.cbegin(); cursor != const_map.cend(); cursor++) {
    EXPECT_TRUE(seen.insert(cursor->first).second);
    EXPECT_EQ(cursor->second, cursor->first * 10 + 1);
  }
  EXPECT_EQ(seen, (std::set<int>{1, 2, 4, 5}));
}

// MP-05：删除的后继按遍历顺序前进，末槽删除不能绕回表头。
TEST(Map, EraseIteratorReturnsSuccessorAcrossSparseSlots) {
  int_map map;
  map.reserve(1000);
  for (int key : {1, 31, 71, 100}) map.try_emplace(int(key), key);
  std::size_t count = 0;
  for (auto it = map.begin(); it != map.end();) {
    auto next = it;
    ++next;
    it = map.erase(it);
    EXPECT_EQ(it, next);
    ++count;
  }
  EXPECT_EQ(count, 4u);
  EXPECT_TRUE(map.empty());
  EXPECT_GT(map.capacity(), 0u);
  EXPECT_EQ(map.cbegin(), map.cend());
  EXPECT_EQ(map.erase(1), 0u);
}

TEST(Map, EraseLastPhysicalSlotDoesNotWrapSuccessor) {
  qlib::map<int, int, collision_hash> map;
  for (int key = 0; key < 3; ++key) map.try_emplace(int(key), key);
  const auto result = map.erase(map.find(1));
  EXPECT_EQ(result, map.end());
  EXPECT_EQ(map.at(2), 2);
  EXPECT_EQ(map.at(0), 0);
}

TEST(Map, EraseKeepsOtherElementAddresses) {
  qlib::map<int, int, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  auto* survivor = &*map.find(5);
  auto survivor_iterator = map.find(5);
  map.erase(0);
  map.erase(2);
  EXPECT_EQ(&*map.find(5), survivor);
  EXPECT_EQ(survivor_iterator->second, 5);
  EXPECT_EQ(map.erase(999), 0u);
}

TEST_F(map_lifetime, ClearDestroysOnlyLiveValuesAndAllowsReuse) {
  qlib::map<int, tracked, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  EXPECT_EQ(tracked::constructed, 6);
  map.erase(0);
  map.erase(2);
  EXPECT_EQ(tracked::destroyed, 2);
  EXPECT_EQ(tracked::live, 4);
  map.clear();
  EXPECT_EQ(tracked::destroyed, 6);
  EXPECT_EQ(tracked::live, 0);
  EXPECT_EQ(map.size(), 0u);
  EXPECT_EQ(map.capacity(), 0u);
  EXPECT_EQ(map.begin(), map.end());
  map.clear();
  EXPECT_EQ(tracked::destroyed, 6);
  map.try_emplace(7, 70);
  EXPECT_EQ(map.at(7).value, 70);
}

TEST_F(map_lifetime, GrowthBalancesEachConstructedInstance) {
  qlib::map<int, tracked> map;
  for (int key = 0; key < 7; ++key) map.try_emplace(int(key), key);
  EXPECT_EQ(tracked::live, 7);
  EXPECT_EQ(tracked::moved, 6);
  EXPECT_EQ(tracked::constructed, 13);
  EXPECT_EQ(tracked::destroyed, 6);
  map.erase(3);
  EXPECT_EQ(tracked::destroyed, 7);
  map.clear();
  EXPECT_EQ(tracked::destroyed, 13);
}

TEST_F(map_lifetime, MoveTransfersStorageAndDestroysOverwrittenTarget) {
  qlib::map<int, tracked> source;
  source.try_emplace(1, 10);
  source.try_emplace(2, 20);
  const int moved = tracked::moved;
  qlib::map<int, tracked> destination(std::move(source));
  EXPECT_EQ(tracked::moved, moved);
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(source.capacity(), 0u);
  EXPECT_EQ(source.begin(), source.end());
  EXPECT_FALSE(source.contains(1));
  EXPECT_EQ(destination.at(2).value, 20);
  source.try_emplace(3, 30);
  EXPECT_EQ(&(source = std::move(destination)), &source);
  EXPECT_EQ(tracked::destroyed, 1);
  EXPECT_EQ(tracked::live, 2);
  EXPECT_EQ(tracked::moved, moved);
  EXPECT_TRUE(destination.empty());
  EXPECT_EQ(destination.capacity(), 0u);
  EXPECT_EQ(source.at(1).value, 10);
  auto& self = source;
  source = std::move(self);
  EXPECT_EQ(source.size(), 2u);
  EXPECT_EQ(tracked::destroyed, 1);
}

TEST(Map, EmptyMoveAssignmentAndSwapCombinations) {
  int_map first;
  int_map second(std::move(first));
  EXPECT_EQ(first.capacity(), 0u);
  EXPECT_EQ(second.capacity(), 0u);
  first = std::move(second);
  first.try_emplace(1, 10);
  first.swap(first);
  EXPECT_EQ(first.at(1), 10);
  first.swap(second);
  EXPECT_TRUE(first.empty());
  EXPECT_EQ(second.at(1), 10);
  qlib::swap(first, second);
  EXPECT_EQ(first.at(1), 10);
  EXPECT_TRUE(second.empty());
  second.try_emplace(2, 20);
  qlib::swap(first, second);
  EXPECT_EQ(first.at(2), 20);
  EXPECT_EQ(second.at(1), 10);
  int_map empty;
  first = std::move(empty);
  EXPECT_TRUE(first.empty());
  EXPECT_EQ(first.capacity(), 0u);
  first.swap(empty);
  EXPECT_TRUE(first.empty());
  EXPECT_TRUE(empty.empty());
}

TEST(Map, StatefulHashAndEqualityFollowStorage) {
  using policy_map = qlib::map<int, int, stateful_hash, stateful_equal>;
  policy_config::modulus = 10;
  policy_config::salt = 7;
  policy_map first;
  first.try_emplace(1, 10);
  policy_config::modulus = 100;
  policy_config::salt = 27;
  policy_map second;
  second.try_emplace(21, 20);
  first.swap(second);
  EXPECT_EQ(first.at(121), 20);
  EXPECT_EQ(second.at(11), 10);
  EXPECT_FALSE(first.contains(1));
  policy_map moved(std::move(second));
  EXPECT_EQ(moved.at(31), 10);
  first = std::move(moved);
  EXPECT_EQ(first.at(41), 10);
  EXPECT_FALSE(first.try_emplace(51, 999).second);
  second.try_emplace(3, 30);
  EXPECT_EQ(second.at(13), 30);
  first.clear();
  first.try_emplace(4, 40);
  EXPECT_EQ(first.at(14), 40);
  policy_config::modulus = 10;
  policy_config::salt = 7;
}

TEST(Map, MoveOnlyKeysAndNonDefaultValuesSurviveGrowth) {
  qlib::map<move_key, aligned_value, move_key_hash, move_key_equal> map;
  for (int key = 0; key < 100; ++key) map.try_emplace(move_key(key), key * 2);
  for (int key = 0; key < 100; ++key) {
    const auto& value = map.at(move_key(key));
    EXPECT_EQ(value.value, key * 2);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&value) % alignof(aligned_value), 0u);
  }
  move_key duplicate(1);
  EXPECT_FALSE(map.try_emplace(std::move(duplicate), 999).second);
  EXPECT_EQ(duplicate.value, 1);
  map.reserve(500);
  for (int key = 0; key < 100; ++key) EXPECT_EQ(map.at(move_key(key)).value, key * 2);
}

TEST_F(map_lifetime, SmartPointerOwnsValueButRawPointerDoesNot) {
  auto external = std::make_unique<tracked>(10);
  {
    qlib::map<int, tracked*> raw;
    raw.try_emplace(1, external.get());
    raw.erase(1);
    raw.try_emplace(2, external.get());
    raw.clear();
    EXPECT_EQ(tracked::live, 1);
    qlib::map<int, std::unique_ptr<tracked>> owned;
    owned.try_emplace(1, std::make_unique<tracked>(20));
    EXPECT_EQ(tracked::live, 2);
    owned.erase(1);
    EXPECT_EQ(tracked::live, 1);
    owned.try_emplace(2, std::make_unique<tracked>(30));
  }
  EXPECT_EQ(tracked::live, 1);
}

TEST_F(map_lifetime, NewValueFailureLeavesEmptySlotUnpublished) {
  qlib::map<int, tracked> map;
  map.reserve(6);
  map.try_emplace(1, 10);
  tracked::fail = true;
  EXPECT_THROW(map.try_emplace(2, 20), std::runtime_error);
  tracked::fail = false;
  EXPECT_EQ(map.size(), 1u);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::live, 1);
  EXPECT_FALSE(map.contains(2));
  EXPECT_EQ(map.at(1).value, 10);
  for (int key = 2; key <= 6; ++key) EXPECT_TRUE(map.try_emplace(int(key), key).second);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, 0);
}

TEST_F(map_lifetime, NewValueFailurePreservesTombstoneAndProbeChain) {
  qlib::map<int, tracked, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  map.erase(0);
  tracked::fail = true;
  EXPECT_THROW(map.try_emplace(9, 90), std::runtime_error);
  tracked::fail = false;
  EXPECT_EQ(map.size(), 5u);
  EXPECT_EQ(tracked::live, 5);
  EXPECT_FALSE(map.contains(9));
  for (int key = 1; key < 6; ++key) EXPECT_EQ(map.at(key).value, key);
  EXPECT_TRUE(map.try_emplace(9, 90).second);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, 0);
  EXPECT_EQ(map.size(), 6u);
}

TEST_F(map_lifetime, NewValueFailureAfterGrowthKeepsMigratedValues) {
  qlib::map<int, tracked, collision_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  tracked::fail = true;
  EXPECT_THROW(map.try_emplace(6, 60), std::runtime_error);
  tracked::fail = false;
  EXPECT_EQ(map.capacity(), 16u);
  EXPECT_EQ(map.size(), 6u);
  EXPECT_EQ(tracked::live, 6);
  EXPECT_EQ(tracked::moved, 6);
  EXPECT_EQ(tracked::destroyed, 6);
  EXPECT_FALSE(map.contains(6));
  for (int key = 0; key < 6; ++key) EXPECT_EQ(map.at(key).value, key);
  EXPECT_TRUE(map.try_emplace(6, 60).second);
  EXPECT_EQ(map.at(6).value, 60);
}

TEST_F(map_lifetime, NewValueFailureAfterSameCapacityCleanupKeepsSurvivors) {
  qlib::map<int, tracked, split_hash> map;
  for (int key = 0; key < 6; ++key) map.try_emplace(int(key), key);
  for (int key = 0; key < 4; ++key) map.erase(key);
  tracked::fail = true;
  EXPECT_THROW(map.try_emplace(100, 100), std::runtime_error);
  tracked::fail = false;
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(map.size(), 2u);
  EXPECT_EQ(tracked::moved, 2);
  EXPECT_EQ(tracked::live, 2);
  EXPECT_EQ(map.at(4).value, 4);
  EXPECT_EQ(map.at(5).value, 5);
  for (int key = 100; key < 104; ++key) map.try_emplace(int(key), key);
  EXPECT_EQ(map.size(), 6u);
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(tracked::moved, 2);
}

TEST_F(map_lifetime, FirstInsertionFailureLeavesReusableEmptyAllocation) {
  qlib::map<int, tracked> map;
  tracked::fail = true;
  EXPECT_THROW(map.try_emplace(1, 10), std::runtime_error);
  tracked::fail = false;
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.capacity(), 8u);
  EXPECT_EQ(map.begin(), map.end());
  EXPECT_EQ(tracked::constructed, 0);
  EXPECT_TRUE(map.try_emplace(1, 10).second);
}

// 固定种子操作序列与标准库独立比较；不要求两张表使用相同遍历顺序。
TEST(Map, FixedSeedDifferentialOneHundredThousandOperations) {
  qlib::map<int, int, collision_hash> actual;
  std::unordered_map<int, int> expected;
  std::mt19937 random(0x514C4942u);
  for (int step = 0; step < 100000; ++step) {
    const int key = static_cast<int>(random() % 257);
    const int value = static_cast<int>(random() % 1000000);
    switch (random() % 10) {
      case 0: {
        auto a = actual.insert({key, value});
        auto e = expected.insert({key, value});
        ASSERT_EQ(a.second, e.second) << step;
        ASSERT_EQ(a.first->second, e.first->second) << step;
        break;
      }
      case 1: {
        auto a = actual.emplace(key, value);
        auto e = expected.emplace(key, value);
        ASSERT_EQ(a.second, e.second) << step;
        ASSERT_EQ(a.first->second, e.first->second) << step;
        break;
      }
      case 2: {
        auto a = actual.try_emplace(int(key), value);
        auto e = expected.try_emplace(key, value);
        ASSERT_EQ(a.second, e.second) << step;
        ASSERT_EQ(a.first->second, e.first->second) << step;
        break;
      }
      case 3:
        actual[int(key)] = value;
        expected[key] = value;
        break;
      case 4:
        ASSERT_EQ(actual.erase(key), expected.erase(key)) << step;
        break;
      case 5: {
        auto a = actual.find(key);
        auto e = expected.find(key);
        ASSERT_EQ(a == actual.end(), e == expected.end()) << step;
        ASSERT_EQ(actual.contains(key), expected.contains(key)) << step;
        if (e != expected.end()) {
          ASSERT_EQ(a->second, e->second) << step;
        }
        break;
      }
      case 6: {
        const auto target = random() % 400;
        actual.reserve(target);
        expected.reserve(target);
        break;
      }
      case 7: {
        auto a = actual.find(key);
        if (a != actual.end()) {
          auto next = a;
          ++next;
          ASSERT_EQ(actual.erase(a), next) << step;
          expected.erase(key);
        }
        break;
      }
      case 8:
        if (expected.contains(key)) {
          actual.at(key) += 1;
          expected.at(key) += 1;
        } else {
          EXPECT_THROW(actual.at(key), std::out_of_range);
        }
        break;
      case 9:
        if ((random() % 100) == 0) {
          actual.clear();
          expected.clear();
        }
        break;
    }
    ASSERT_EQ(actual.size(), expected.size()) << step;
    if (step % 101 == 0 || step == 99999) {
      std::unordered_map<int, int> observed;
      for (const auto& item : actual) ASSERT_TRUE(observed.emplace(item).second) << step;
      ASSERT_EQ(observed, expected) << step;
      for (const auto& [k, v] : expected) ASSERT_EQ(actual.at(k), v) << step;
    }
  }
}

}  // namespace

namespace {

// 组合操作由调用方完成；辅助函数只存在于测试中。
template <typename T, typename BeforeIndex>
bool add_indexed(qlib::hive<T>& objects,
                 qlib::map<std::int64_t, T*>& by_id,
                 const T& input, BeforeIndex before_index) {
  auto pos = objects.emplace(input);
  bool inserted = false;
  try {
    before_index();
    inserted = by_id.try_emplace(std::int64_t{input.order_id},
                                 std::addressof(*pos)).second;
  } catch (...) {
    objects.erase(pos);
    throw;
  }
  // 提交成功后不再执行可能抛出的步骤，避免误删已被索引引用的对象。
  if (!inserted) {
    objects.erase(pos);
  }
  return inserted;
}

template <typename T>
bool erase_indexed(qlib::hive<T>& objects,
                   qlib::map<std::int64_t, T*>& by_id,
                   std::int64_t id) {
  const auto found = by_id.find(id);
  if (found == by_id.end()) {
    return false;
  }
  T* object = found->second;
  by_id.erase(found);
  objects.erase(objects.get_iterator(object));
  return true;
}

qlib::order make_order(std::int64_t id, std::int64_t quantity = 2) {
  qlib::order input{};
  input.seq = id + 1000;
  input.order_id = id;
  input.instrument_id = 7;
  input.side = qlib::side::buy;
  input.type = qlib::order_type::limit;
  input.action = qlib::order_action::add;
  input.price = 100 * qlib::price_scale;
  input.qty = quantity * qlib::qty_scale;
  input.new_qty = 0;
  input.prev = nullptr;
  input.next = nullptr;
  input.event_ns = 1'000'000 + id;
  input.recv_ns = 1'000'100 + id;
  return input;
}

TEST(HiveIndexIntegration, OrdersKeepAddressesAcrossGrowthAndRemoval) {
  qlib::hive<qlib::order> objects;
  qlib::map<std::int64_t, qlib::order*> by_id;
  std::vector<qlib::order*> addresses;
  for (std::int64_t id = 1; id <= 512; ++id) {
    ASSERT_TRUE(add_indexed(objects, by_id, make_order(id), [] {}));
    addresses.push_back(by_id.at(id));
  }
  ASSERT_EQ(objects.size(), 512U);
  ASSERT_EQ(by_id.size(), 512U);
  for (const std::int64_t id : {1, 256, 512}) {
    EXPECT_TRUE(erase_indexed(objects, by_id, id));
    EXPECT_FALSE(erase_indexed(objects, by_id, id));
  }
  EXPECT_FALSE(erase_indexed(objects, by_id, 999));
  EXPECT_EQ(objects.size(), 509U);
  EXPECT_EQ(by_id.size(), 509U);
  for (std::int64_t id = 2; id < 512; ++id) {
    if (id == 256) {
      continue;
    }
    const auto found = by_id.find(id);
    ASSERT_NE(found, by_id.end());
    EXPECT_EQ(found->second, addresses[static_cast<std::size_t>(id - 1)]);
    EXPECT_EQ(found->second->order_id, id);
    EXPECT_EQ(found->second->qty, 200'000'000);
    EXPECT_EQ(found->second->price, 1'000'000'000'000);
    EXPECT_EQ(found->second->prev, nullptr);
    EXPECT_EQ(found->second->next, nullptr);
  }
  by_id.at(42)->qty = 300'000'000;
  EXPECT_EQ(objects.get_iterator(by_id.at(42))->qty, 300'000'000);
  for (const auto& item : objects) {
    EXPECT_EQ(by_id.at(item.order_id), std::addressof(item));
  }
  by_id.clear();
  EXPECT_EQ(objects.size(), 509U);
  objects.clear();
  EXPECT_EQ(by_id.capacity(), 0U);
  EXPECT_EQ(objects.capacity(), 0U);
}

TEST(HiveIndexIntegration, DuplicateIdKeepsOriginalOrderAndReclaimsCandidate) {
  qlib::hive<qlib::order> objects;
  qlib::map<std::int64_t, qlib::order*> by_id;
  ASSERT_TRUE(add_indexed(objects, by_id, make_order(42), [] {}));
  const auto* original = by_id.at(42);
  EXPECT_FALSE(add_indexed(objects, by_id, make_order(42, 9), [] {}));
  EXPECT_EQ(objects.size(), 1U);
  EXPECT_EQ(by_id.size(), 1U);
  EXPECT_EQ(by_id.at(42), original);
  EXPECT_EQ(original->qty, 200'000'000);
  EXPECT_TRUE(erase_indexed(objects, by_id, 42));
  EXPECT_TRUE(objects.empty());
  EXPECT_TRUE(by_id.empty());
}

TEST(HiveIndexIntegration, FailureBeforeIndexPreservesOriginalRelations) {
  qlib::hive<qlib::order> objects;
  qlib::map<std::int64_t, qlib::order*> by_id;
  ASSERT_TRUE(add_indexed(objects, by_id, make_order(42), [] {}));
  const auto* original = by_id.at(42);
  EXPECT_THROW(add_indexed(objects, by_id, make_order(43), [] {
    throw std::runtime_error("before index");
  }), std::runtime_error);
  EXPECT_EQ(objects.size(), 1U);
  EXPECT_EQ(by_id.size(), 1U);
  EXPECT_FALSE(by_id.contains(43));
  EXPECT_EQ(by_id.at(42), original);
  EXPECT_EQ(original->order_id, 42);
  EXPECT_TRUE(add_indexed(objects, by_id, make_order(43), [] {}));
  EXPECT_EQ(objects.size(), 2U);
}

struct IndexedLifetime {
  int constructed = 0;
  int destroyed = 0;
  int live = 0;
  bool fail_copy = false;
  std::function<bool(std::int64_t)> is_indexed;
  int destroyed_while_indexed = 0;
};

struct CountedOrder {
  IndexedLifetime* lifetime;
  std::int64_t order_id;

  CountedOrder(IndexedLifetime& state, std::int64_t id) noexcept
      : lifetime(&state), order_id(id) {
    ++lifetime->constructed;
    ++lifetime->live;
  }

  CountedOrder(const CountedOrder& other)
      : lifetime(other.lifetime), order_id(other.order_id) {
    if (lifetime->fail_copy) {
      throw std::runtime_error("order construction");
    }
    ++lifetime->constructed;
    ++lifetime->live;
  }

  ~CountedOrder() noexcept {
    if (lifetime->is_indexed && lifetime->is_indexed(order_id)) {
      ++lifetime->destroyed_while_indexed;
    }
    ++lifetime->destroyed;
    --lifetime->live;
  }
};

TEST(HiveIndexIntegration, FailedConstructionDuplicateAndCallbackBalanceLifetimes) {
  IndexedLifetime state;
  {
    qlib::hive<CountedOrder> objects;
    qlib::map<std::int64_t, CountedOrder*> by_id;
    CountedOrder input(state, 42);
    ASSERT_TRUE(add_indexed(objects, by_id, input, [] {}));
    EXPECT_EQ(state.live, 2);
    EXPECT_FALSE(add_indexed(objects, by_id, input, [] {}));
    EXPECT_EQ(state.live, 2);
    EXPECT_EQ(state.destroyed, 1);

    input.order_id = 43;
    state.fail_copy = true;
    EXPECT_THROW(add_indexed(objects, by_id, input, [] {}), std::runtime_error);
    state.fail_copy = false;
    EXPECT_EQ(state.live, 2);
    EXPECT_EQ(state.destroyed, 1);
    EXPECT_THROW(add_indexed(objects, by_id, input, [] {
      throw std::runtime_error("index not committed");
    }), std::runtime_error);
    EXPECT_EQ(state.live, 2);
    EXPECT_EQ(state.destroyed, 2);
    EXPECT_EQ(objects.size(), 1U);
    EXPECT_EQ(by_id.size(), 1U);
    EXPECT_EQ(by_id.at(42)->order_id, 42);
    EXPECT_FALSE(by_id.contains(43));
    EXPECT_TRUE(add_indexed(objects, by_id, input, [] {}));
  }
  EXPECT_EQ(state.live, 0);
  EXPECT_EQ(state.constructed, state.destroyed);
  EXPECT_EQ(state.constructed, 5);
}

TEST(HiveIndexIntegration, RemovingIndexDoesNotDestroyObject) {
  IndexedLifetime state;
  qlib::hive<CountedOrder> objects;
  qlib::map<std::int64_t, CountedOrder*> by_id;
  auto position = objects.emplace(state, 42);
  auto* address = std::addressof(*position);
  ASSERT_TRUE(by_id.try_emplace(42, address).second);
  EXPECT_EQ(by_id.erase(42), 1U);
  EXPECT_FALSE(by_id.contains(42));
  EXPECT_EQ(state.live, 1);
  EXPECT_EQ(state.destroyed, 0);
  EXPECT_EQ(address->order_id, 42);
  objects.erase(objects.get_iterator(address));
  EXPECT_EQ(state.live, 0);
  EXPECT_EQ(state.destroyed, 1);
}

TEST(HiveIndexIntegration, EraseByIdRemovesBorrowedPointerBeforeDestructor) {
  IndexedLifetime state;
  qlib::hive<CountedOrder> objects;
  qlib::map<std::int64_t, CountedOrder*> by_id;
  // 失败提前退出时，先解除观察回调，避免它访问已销毁的索引。
  struct observation_guard {
    IndexedLifetime& state;
    ~observation_guard() { state.is_indexed = {}; }
  } guard{state};
  state.is_indexed = [&by_id](std::int64_t id) { return by_id.contains(id); };
  auto pos = objects.emplace(state, 42);
  ASSERT_TRUE(by_id.try_emplace(42, std::addressof(*pos)).second);
  EXPECT_TRUE(erase_indexed(objects, by_id, 42));
  EXPECT_EQ(state.destroyed_while_indexed, 0);
  EXPECT_EQ(state.destroyed, 1);
  EXPECT_EQ(state.live, 0);
  EXPECT_TRUE(objects.empty());
  EXPECT_TRUE(by_id.empty());
}

TEST(HiveIndexIntegration, ClearAndReuseDestroyEachObjectOnce) {
  IndexedLifetime state;
  {
    qlib::hive<CountedOrder> objects;
    qlib::map<std::int64_t, CountedOrder*> by_id;
    for (std::int64_t id = 1; id <= 20; ++id) {
      auto pos = objects.emplace(state, id);
      ASSERT_TRUE(by_id.try_emplace(std::int64_t{id}, std::addressof(*pos)).second);
    }
    by_id.clear();
    EXPECT_EQ(state.live, 20);
    objects.clear();
    EXPECT_EQ(state.live, 0);
    EXPECT_EQ(state.destroyed, 20);
    by_id.clear();
    objects.clear();
    EXPECT_EQ(state.destroyed, 20);
    EXPECT_EQ(objects.capacity(), 0U);
    EXPECT_EQ(by_id.capacity(), 0U);
    auto pos = objects.emplace(state, 21);
    ASSERT_TRUE(by_id.try_emplace(21, std::addressof(*pos)).second);
    EXPECT_EQ(state.live, 1);
    // 声明顺序确保退出作用域时，索引先于拥有对象的容器销毁。
  }
  EXPECT_EQ(state.live, 0);
  EXPECT_EQ(state.constructed, 21);
  EXPECT_EQ(state.destroyed, 21);
}

}  // namespace
