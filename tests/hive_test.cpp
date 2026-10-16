#include <qlib/hive.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using int_hive = qlib::hive<int>;
static_assert(std::same_as<int_hive::value_type, int>);
static_assert(std::same_as<int_hive::size_type, std::size_t>);
static_assert(std::same_as<int_hive::difference_type, std::ptrdiff_t>);
static_assert(std::same_as<int_hive::reference, int&>);
static_assert(std::same_as<int_hive::const_reference, const int&>);
static_assert(std::same_as<int_hive::pointer, int*>);
static_assert(std::same_as<int_hive::const_pointer, const int*>);
static_assert(std::same_as<int_hive::reverse_iterator,
                           std::reverse_iterator<int_hive::iterator>>);
static_assert(std::same_as<int_hive::const_reverse_iterator,
                           std::reverse_iterator<int_hive::const_iterator>>);
static_assert(std::bidirectional_iterator<int_hive::iterator>);
static_assert(std::bidirectional_iterator<int_hive::const_iterator>);
static_assert(std::convertible_to<int_hive::iterator, int_hive::const_iterator>);
static_assert(!std::convertible_to<int_hive::const_iterator, int_hive::iterator>);
static_assert(std::same_as<decltype(*std::declval<int_hive::const_iterator>()), const int&>);
static_assert(!std::is_copy_constructible_v<int_hive>);
static_assert(!std::is_copy_assignable_v<int_hive>);
static_assert(std::is_nothrow_default_constructible_v<int_hive>);
static_assert(std::is_nothrow_move_constructible_v<int_hive>);
static_assert(std::is_nothrow_move_assignable_v<int_hive>);
static_assert(!noexcept(std::declval<int_hive&>().erase(
    std::declval<int_hive::const_iterator>())));
static_assert(!noexcept(std::declval<int_hive&>().erase(
    std::declval<int_hive::const_iterator>(), std::declval<int_hive::const_iterator>())));
static_assert(noexcept(std::declval<int_hive&>().clear()));
static_assert(noexcept(std::declval<int_hive&>().swap(std::declval<int_hive&>())));
static_assert(noexcept(qlib::swap(std::declval<int_hive&>(), std::declval<int_hive&>())));

template <class H>
constexpr bool observer_contract() {
  return noexcept(std::declval<H&>().begin()) && noexcept(std::declval<H&>().end()) &&
         noexcept(std::declval<H&>().cbegin()) && noexcept(std::declval<H&>().cend()) &&
         noexcept(std::declval<H&>().rbegin()) && noexcept(std::declval<H&>().rend()) &&
         noexcept(std::declval<H&>().empty()) && noexcept(std::declval<H&>().size()) &&
         noexcept(std::declval<H&>().capacity()) && noexcept(std::declval<H&>().max_size()) &&
         noexcept(std::declval<H&>().get_iterator(static_cast<const int*>(nullptr)));
}
static_assert(observer_contract<int_hive>());
static_assert(observer_contract<const int_hive>());
static_assert(std::same_as<decltype(std::declval<const int_hive&>().begin()),
                           int_hive::const_iterator>);
static_assert(std::same_as<decltype(std::declval<const int_hive&>().get_iterator(nullptr)),
                           int_hive::const_iterator>);

template <class H>
concept accepts_copy_insert = requires(H& values, const typename H::value_type& value) {
  values.insert(value);
};
static_assert(!accepts_copy_insert<int_hive>);

void expect_contents(const int_hive& values, const std::set<int>& expected) {
  const std::vector<int> forward(values.cbegin(), values.cend());
  const std::vector<int> reverse(values.rbegin(), values.rend());
  EXPECT_EQ(forward.size(), expected.size());
  EXPECT_EQ(values.size(), expected.size());
  EXPECT_EQ(std::set<int>(forward.begin(), forward.end()), expected);
  EXPECT_EQ(std::vector<int>(forward.rbegin(), forward.rend()), reverse);
  for (auto it = values.begin(); it != values.end(); ++it) {
    EXPECT_EQ(values.get_iterator(std::addressof(*it)), it);
    EXPECT_EQ(it.operator->(), std::addressof(*it));
  }
}

struct counted {
  inline static int constructed = 0;
  inline static int destroyed = 0;
  inline static int alive = 0;
  std::array<std::uint64_t, 4> payload;
  int id;

  explicit counted(int value, bool fail = false)
      : payload{0x12345678, 0xABCDEF01, 0xFF00FF00, 0xAABBCCDD}, id(value) {
    if (fail) throw std::runtime_error("构造失败");
    ++constructed;
    ++alive;
  }
  counted(const counted&) = delete;
  counted(counted&&) = delete;
  ~counted() {
    ++destroyed;
    --alive;
  }

  static void reset() {
    constructed = destroyed = alive = 0;
  }
};

std::set<int> counted_contents(const qlib::hive<counted>& values) {
  std::set<int> result;
  for (const auto& value : values) result.insert(value.id);
  EXPECT_EQ(result.size(), values.size());
  std::vector<int> forward;
  std::vector<int> reverse;
  for (const auto& value : values) forward.push_back(value.id);
  for (auto it = values.rbegin(); it != values.rend(); ++it) reverse.push_back(it->id);
  EXPECT_EQ(std::vector<int>(forward.rbegin(), forward.rend()), reverse);
  return result;
}

TEST(HiveHV01, EmptyQueriesAndRepeatedClear) {
  int_hive values;
  EXPECT_TRUE(values.empty());
  EXPECT_EQ(values.size(), 0u);
  EXPECT_EQ(values.capacity(), 0u);
  EXPECT_EQ(values.max_size(), std::numeric_limits<std::size_t>::max() / sizeof(int));
  EXPECT_EQ(values.begin(), values.end());
  EXPECT_EQ(values.cbegin(), values.cend());
  EXPECT_EQ(values.rbegin(), values.rend());
  const auto& read_only = values;
  EXPECT_EQ(read_only.begin(), read_only.end());
  EXPECT_EQ(read_only.rbegin(), read_only.rend());
  EXPECT_EQ(values.get_iterator(nullptr), values.end());
  values.clear();
  values.clear();
  EXPECT_EQ(*values.emplace(42), 42);
  values.clear();
  EXPECT_TRUE(values.empty());
  EXPECT_EQ(values.capacity(), 0u);
}

TEST(HiveHV01, CapacityGrowsFromEightAndCapsEachNewBlockAt8192) {
  int_hive values;
  const std::array<std::pair<int, std::size_t>, 8> boundaries{{
      {1, 8}, {9, 16}, {17, 32}, {33, 64},
      {8193, 16384}, {16385, 24576}, {24577, 32768}, {32769, 40960}}};
  int inserted = 0;
  for (const auto& [count, capacity] : boundaries) {
    while (inserted < count) values.emplace(inserted++);
    EXPECT_EQ(values.size(), static_cast<std::size_t>(count));
    EXPECT_EQ(values.capacity(), capacity);
  }
}

TEST(HiveHV01, MoveOnlyInsertAndImmovableEmplace) {
  qlib::hive<std::unique_ptr<int>> values;
  auto item = std::make_unique<int>(17);
  auto position = values.insert(std::move(item));
  EXPECT_EQ(item, nullptr);
  EXPECT_EQ(**position, 17);
  counted::reset();
  {
    qlib::hive<counted> immovable;
    auto keep = immovable.emplace(31);
    for (int i = 0; i < 40; ++i) immovable.emplace(100 + i);
    EXPECT_EQ(keep->id, 31);
    EXPECT_EQ(counted::constructed, 41);
    EXPECT_EQ(counted::destroyed, 0);
  }
  EXPECT_EQ(counted::alive, 0);
  EXPECT_EQ(counted::constructed, counted::destroyed);
}

TEST(HiveHV01, OverAlignedSlotsRemainAlignedThroughGrowthAndReuse) {
  struct alignas(256) aligned_value {
    int value;
    explicit aligned_value(int input) : value(input) {}
    aligned_value(const aligned_value&) = delete;
    aligned_value(aligned_value&&) = delete;
  };
  qlib::hive<aligned_value> values;
  std::vector<aligned_value*> pointers;
  for (int i = 0; i < 100; ++i) pointers.push_back(std::addressof(*values.emplace(i)));
  for (auto* pointer : pointers) {
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignof(aligned_value), 0u);
  }
  auto* kept = pointers.back();
  values.erase(values.get_iterator(pointers[3]));
  auto added = values.emplace(500);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(std::addressof(*added)) % 256, 0u);
  EXPECT_EQ(kept->value, 99);
}

TEST(HiveHV02, AddressesAndForwardIteratorsSurviveGrowthAndOtherErases) {
  int_hive values;
  std::vector<int_hive::iterator> keep;
  std::vector<int*> addresses;
  for (int i = 0; i < 80; ++i) {
    keep.push_back(values.emplace(i));
    addresses.push_back(std::addressof(*keep.back()));
  }
  for (int i = 80; i < 2000; ++i) values.emplace(i);
  for (int i = 1; i < 80; i += 2) values.erase(keep[i]);
  for (int i = 0; i < 80; i += 2) {
    EXPECT_EQ(*keep[i], i);
    EXPECT_EQ(std::addressof(*keep[i]), addresses[i]);
    EXPECT_EQ(values.get_iterator(addresses[i]), keep[i]);
    auto next = keep[i];
    ++next;
    ASSERT_NE(next, values.end());
    EXPECT_NE(*next, i);
  }
}

TEST(HiveHV03, IteratorConversionPostfixAndReverseAcrossBlocksAndHoles) {
  int_hive values;
  std::set<int> expected;
  std::vector<int*> pointers;
  for (int i = 0; i < 70; ++i) {
    pointers.push_back(std::addressof(*values.emplace(i)));
    expected.insert(i);
  }
  for (int i : {0, 1, 6, 7, 8, 9, 14, 15, 16, 29, 30, 31, 32, 63, 64, 69}) {
    values.erase(values.get_iterator(pointers[i]));
    expected.erase(i);
  }
  expect_contents(values, expected);
  auto it = values.begin();
  int_hive::const_iterator converted = it;
  EXPECT_EQ(converted, it);
  EXPECT_EQ(it, converted);
  auto old = it++;
  EXPECT_EQ(old, values.begin());
  EXPECT_EQ(--it, old);
  auto tail = values.end();
  auto old_tail = tail--;
  EXPECT_EQ(old_tail, values.end());
  EXPECT_EQ(*tail, *values.rbegin());
  auto saved = tail;
  auto previous = tail--;
  EXPECT_EQ(previous, saved);
  EXPECT_EQ(++tail, saved);
  const auto& read_only = values;
  for (auto current = values.begin(); current != values.end(); ++current) {
    EXPECT_EQ(read_only.get_iterator(std::addressof(*current)), current);
  }
}

TEST(HiveHV03, RangeEraseReturnsFollowingElementAcrossBlocks) {
  int_hive values;
  std::set<int> expected;
  for (int i = 0; i < 50; ++i) {
    values.emplace(i);
    expected.insert(i);
  }
  auto first = std::next(values.cbegin(), 5);
  auto last = std::next(values.cbegin(), 35);
  const int successor = *last;
  auto* successor_address = std::addressof(*last);
  for (auto it = first; it != last; ++it) expected.erase(*it);
  auto result = values.erase(first, last);
  EXPECT_EQ(*result, successor);
  EXPECT_EQ(std::addressof(*result), successor_address);
  EXPECT_EQ(values.erase(result, result), result);
  EXPECT_EQ(values.erase(values.cend(), values.cend()), values.end());
  expect_contents(values, expected);
  EXPECT_EQ(values.erase(values.cbegin(), values.cend()), values.end());
  EXPECT_EQ(values.size(), 0u);
  EXPECT_EQ(values.capacity(), 0u);
}

TEST(HiveHV04, FourHoleMergeCasesPreserveContentsAndReuseAllHoles) {
  const std::vector<std::vector<int>> deletions{{3}, {2, 3}, {3, 2}, {2, 4, 3},
                                              {1, 3, 5, 2, 4}, {5, 3, 1, 2, 4}};
  for (const auto& removed : deletions) {
    int_hive values;
    std::array<int_hive::iterator, 8> positions;
    std::set<int> expected;
    for (int i = 0; i < 8; ++i) {
      positions[i] = values.emplace(i);
      expected.insert(i);
    }
    for (int id : removed) {
      auto next = positions[id];
      ++next;
      EXPECT_EQ(values.erase(positions[id]), next);
      expected.erase(id);
      expect_contents(values, expected);
    }
    for (std::size_t i = 0; i < removed.size(); ++i) {
      values.emplace(100 + static_cast<int>(i));
      expected.insert(100 + static_cast<int>(i));
      EXPECT_EQ(values.capacity(), 8u);
      expect_contents(values, expected);
    }
  }
}

TEST(HiveHV04, MultipleBlocksWithMultipleHolesAreCompletelyReused) {
  int_hive values;
  std::vector<int*> pointers;
  std::set<int> expected;
  for (int i = 0; i < 32; ++i) {
    pointers.push_back(std::addressof(*values.emplace(i)));
    expected.insert(i);
  }
  for (int i : {1, 3, 5, 9, 11, 13, 17, 19, 21, 22, 23, 24, 29}) {
    values.erase(values.get_iterator(pointers[i]));
    expected.erase(i);
  }
  for (int i = 0; i < 13; ++i) {
    values.emplace(100 + i);
    expected.insert(100 + i);
    EXPECT_EQ(values.capacity(), 32u);
    expect_contents(values, expected);
  }
  values.emplace(200);
  EXPECT_EQ(values.capacity(), 64u);
}

TEST(HiveHV04, ErasingHeadMiddleAndTailBlocksReleasesTheirCapacity) {
  for (int removed_block : {0, 1, 2}) {
    int_hive values;
    std::vector<int*> pointers;
    std::set<int> expected;
    for (int i = 0; i < 32; ++i) {
      pointers.push_back(std::addressof(*values.emplace(i)));
      expected.insert(i);
    }
    const int first = removed_block == 0 ? 0 : removed_block == 1 ? 8 : 16;
    const int last = removed_block == 0 ? 8 : removed_block == 1 ? 16 : 32;
    for (int i = first; i < last; ++i) {
      auto current = values.get_iterator(pointers[i]);
      auto successor = current;
      ++successor;
      EXPECT_EQ(values.erase(current), successor);
      expected.erase(i);
    }
    EXPECT_EQ(values.capacity(), static_cast<std::size_t>(32 - (last - first)));
    expect_contents(values, expected);
    values.emplace(200);
    EXPECT_EQ(*values.get_iterator(pointers[first == 0 ? 31 : 0]), first == 0 ? 31 : 0);
  }
}

TEST(HiveHV04, DeterministicChurnMatchesIndependentLiveSet) {
  int_hive values;
  std::unordered_map<int, int*> live;
  std::mt19937 random(1729);
  int id = 0;
  for (int step = 0; step < 3000; ++step) {
    if (live.empty() || random() % 5 < 3) {
      auto it = values.emplace(id);
      live.emplace(id++, std::addressof(*it));
    } else {
      auto victim = live.begin();
      std::advance(victim, random() % live.size());
      values.erase(values.get_iterator(victim->second));
      live.erase(victim);
    }
    if (step % 37 == 0) {
      std::set<int> expected;
      for (const auto& [key, pointer] : live) {
        expected.insert(key);
        EXPECT_EQ(*pointer, key);
        EXPECT_EQ(std::addressof(*values.get_iterator(pointer)), pointer);
      }
      expect_contents(values, expected);
    }
  }
}

TEST(HiveLifecycle, ClearAndDestructorDestroyOnlySuccessfullyConstructedObjects) {
  counted::reset();
  {
    qlib::hive<counted> values;
    std::vector<counted*> pointers;
    for (int i = 0; i < 60; ++i) pointers.push_back(std::addressof(*values.emplace(i)));
    for (int i : {1, 3, 2, 9, 11, 10, 24, 25, 26}) {
      values.erase(values.get_iterator(pointers[i]));
    }
    EXPECT_EQ(counted::destroyed, 9);
    EXPECT_EQ(counted::alive, 51);
    values.clear();
    EXPECT_EQ(counted::alive, 0);
    EXPECT_EQ(counted::constructed, counted::destroyed);
    EXPECT_EQ(values.capacity(), 0u);
    values.clear();
    for (int i = 0; i < 11; ++i) values.emplace(100 + i);
  }
  EXPECT_EQ(counted::alive, 0);
  EXPECT_EQ(counted::constructed, 71);
  EXPECT_EQ(counted::constructed, counted::destroyed);
}

TEST(HiveExceptions, FailedUnusedSlotConstructionPreservesStateAndCanRetry) {
  counted::reset();
  {
    qlib::hive<counted> values;
    auto keep = values.emplace(1);
    auto* address = std::addressof(*keep);
    EXPECT_THROW(values.emplace(2, true), std::runtime_error);
    EXPECT_EQ(values.size(), 1u);
    EXPECT_EQ(values.capacity(), 8u);
    EXPECT_EQ(counted_contents(values), (std::set<int>{1}));
    EXPECT_EQ(std::addressof(*keep), address);
    EXPECT_EQ(counted::constructed, 1);
    EXPECT_EQ(counted::destroyed, 0);
    EXPECT_EQ(values.emplace(3)->id, 3);
    EXPECT_EQ(counted_contents(values), (std::set<int>{1, 3}));
  }
  EXPECT_EQ(counted::alive, 0);
  EXPECT_EQ(counted::constructed, counted::destroyed);
}

TEST(HiveExceptions, FailedHoleConstructionRestoresOverwrittenFreeLinks) {
  counted::reset();
  {
    qlib::hive<counted> values;
    std::vector<counted*> pointers;
    for (int i = 0; i < 8; ++i) pointers.push_back(std::addressof(*values.emplace(i)));
    for (int i : {1, 3, 4}) values.erase(values.get_iterator(pointers[i]));
    EXPECT_THROW(values.emplace(100, true), std::runtime_error);
    EXPECT_EQ(values.size(), 5u);
    EXPECT_EQ(counted_contents(values), (std::set<int>{0, 2, 5, 6, 7}));
    values.emplace(100);
    EXPECT_THROW(values.emplace(101, true), std::runtime_error);
    values.emplace(101);
    values.emplace(102);
    EXPECT_EQ(values.capacity(), 8u);
    EXPECT_EQ(counted_contents(values), (std::set<int>{0, 2, 5, 6, 7, 100, 101, 102}));
  }
  EXPECT_EQ(counted::alive, 0);
  EXPECT_EQ(counted::constructed, counted::destroyed);
}

TEST(HiveExceptions, FailedNewBlockConstructionLeavesTraversableReusableEmptyTail) {
  counted::reset();
  {
    qlib::hive<counted> values;
    for (int i = 0; i < 8; ++i) values.emplace(i);
    auto keep = values.begin();
    EXPECT_THROW(values.emplace(8, true), std::runtime_error);
    EXPECT_EQ(values.size(), 8u);
    EXPECT_EQ(values.capacity(), 16u);
    EXPECT_EQ(counted_contents(values), (std::set<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    EXPECT_EQ(keep, values.begin());
    EXPECT_EQ((--values.end())->id, 7);
    values.emplace(9);
    EXPECT_EQ(values.size(), 9u);
    EXPECT_EQ(values.capacity(), 16u);
    values.clear();
    EXPECT_EQ(counted::alive, 0);
    EXPECT_THROW(values.emplace(10, true), std::runtime_error);
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(values.begin(), values.end());
    EXPECT_EQ(values.rbegin(), values.rend());
    EXPECT_EQ(values.capacity(), 8u);
    values.clear();
    EXPECT_EQ(values.capacity(), 0u);
    EXPECT_THROW(values.emplace(11, true), std::runtime_error);
  }
  EXPECT_EQ(counted::alive, 0);
  EXPECT_EQ(counted::constructed, counted::destroyed);
}

TEST(HiveExceptions, ErasingLastLiveBlockWithFailedEmptyTailAllowsReuse) {
  counted::reset();
  {
    qlib::hive<counted> values;
    for (int i = 0; i < 8; ++i) values.emplace(i);
    EXPECT_THROW(values.emplace(100, true), std::runtime_error);
    values.erase(values.cbegin(), values.cend());
    EXPECT_TRUE(values.empty());
    EXPECT_EQ(values.begin(), values.end());
    EXPECT_EQ(values.rbegin(), values.rend());
    EXPECT_EQ(values.emplace(200)->id, 200);
    EXPECT_EQ(values.size(), 1u);
  }
  EXPECT_EQ(counted::alive, 0);
  EXPECT_EQ(counted::constructed, counted::destroyed);
}

TEST(HiveHV01, MovesTransferOwnershipHolesAndAllowSourceReuse) {
  counted::reset();
  {
    qlib::hive<counted> source;
    std::vector<counted*> pointers;
    for (int i = 0; i < 12; ++i) pointers.push_back(std::addressof(*source.emplace(i)));
    source.erase(source.get_iterator(pointers[2]));
    const auto capacity = source.capacity();
    qlib::hive<counted> target(std::move(source));
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.capacity(), 0u);
    EXPECT_EQ(target.capacity(), capacity);
    EXPECT_EQ(std::addressof(*target.get_iterator(pointers[0])), pointers[0]);
    target.emplace(100);
    EXPECT_EQ(target.capacity(), capacity);
    source.emplace(200);
    const auto destroyed = counted::destroyed;
    EXPECT_EQ(std::addressof(source = std::move(target)), std::addressof(source));
    EXPECT_EQ(counted::destroyed, destroyed + 1);
    EXPECT_TRUE(target.empty());
    EXPECT_EQ(target.capacity(), 0u);
    target.emplace(300);
    auto* first = std::addressof(*source.begin());
    auto& alias = source;
    source = std::move(alias);
    EXPECT_EQ(std::addressof(*source.begin()), first);
    EXPECT_EQ(source.size(), 12u);
  }
  EXPECT_EQ(counted::alive, 0);
  EXPECT_EQ(counted::constructed, counted::destroyed);
}

TEST(HiveHV01, MemberAndFreeSwapExchangeContentsAndEmptyStates) {
  int_hive first;
  int_hive second;
  auto* a = std::addressof(*first.emplace(1));
  auto hole = first.emplace(2);
  first.erase(hole);
  for (int i = 10; i < 20; ++i) second.emplace(i);
  auto* b = std::addressof(*second.begin());
  first.swap(second);
  EXPECT_EQ(std::addressof(*first.get_iterator(b)), b);
  EXPECT_EQ(std::addressof(*second.get_iterator(a)), a);
  second.emplace(3);
  EXPECT_EQ(second.capacity(), 8u);
  expect_contents(second, {1, 3});
  qlib::swap(first, second);
  expect_contents(first, {1, 3});
  first.swap(first);
  expect_contents(first, {1, 3});
  int_hive empty;
  qlib::swap(first, empty);
  EXPECT_TRUE(first.empty());
  EXPECT_EQ(first.capacity(), 0u);
  expect_contents(empty, {1, 3});
  first.emplace(400);
  EXPECT_EQ(*first.begin(), 400);
  int_hive moved_empty(std::move(second));
  int_hive empty_again(std::move(second));
  EXPECT_TRUE(empty_again.empty());
  EXPECT_EQ(empty_again.capacity(), 0u);
}

}  // namespace
