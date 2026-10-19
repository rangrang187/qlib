#include <qlib/memory_pool.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using memory_pool = qlib::memory_pool;
using size_type = memory_pool::size_type;

static_assert(std::same_as<size_type, std::size_t>);
static_assert(!std::is_default_constructible_v<memory_pool>);
static_assert(!std::is_copy_constructible_v<memory_pool>);
static_assert(!std::is_copy_assignable_v<memory_pool>);
static_assert(!std::is_move_constructible_v<memory_pool>);
static_assert(!std::is_move_assignable_v<memory_pool>);
static_assert(std::is_nothrow_destructible_v<memory_pool>);
static_assert(!std::is_nothrow_constructible_v<memory_pool, size_type, size_type>);
static_assert(!std::is_nothrow_constructible_v<memory_pool, size_type, size_type, size_type>);
static_assert(std::same_as<decltype(&memory_pool::allocate), void* (memory_pool::*)() noexcept>);
static_assert(std::same_as<decltype(&memory_pool::deallocate),
                           void (memory_pool::*)(void*) noexcept>);
static_assert(std::same_as<decltype(&memory_pool::block_size),
                           size_type (memory_pool::*)() const noexcept>);
static_assert(std::same_as<decltype(&memory_pool::alignment),
                           size_type (memory_pool::*)() const noexcept>);
static_assert(std::same_as<decltype(&memory_pool::empty),
                           bool (memory_pool::*)() const noexcept>);
static_assert(std::same_as<decltype(&memory_pool::size),
                           size_type (memory_pool::*)() const noexcept>);
static_assert(std::same_as<decltype(&memory_pool::capacity),
                           size_type (memory_pool::*)() const noexcept>);

TEST(MemoryPool, Mem01DefaultAlignmentAndInitialState) {
  memory_pool storage(1, 5);
  const memory_pool& observed = storage;
  EXPECT_EQ(observed.alignment(), alignof(std::max_align_t));
  EXPECT_EQ(observed.block_size(), alignof(std::max_align_t));
  EXPECT_EQ(observed.capacity(), 5u);
  EXPECT_EQ(observed.size(), 0u);
  EXPECT_TRUE(observed.empty());
}

struct layout_case {
  size_type requested_size;
  size_type requested_alignment;
  size_type expected_size;
  size_type expected_alignment;
};

class MemoryPoolLayout : public testing::TestWithParam<layout_case> {};

TEST_P(MemoryPoolLayout, Mem01NormalizesSizeAndAlignment) {
  const auto config = GetParam();
  memory_pool storage(config.requested_size, 3, config.requested_alignment);
  const memory_pool& observed = storage;
  EXPECT_EQ(observed.block_size(), config.expected_size);
  EXPECT_EQ(observed.alignment(), config.expected_alignment);
  EXPECT_GE(observed.block_size(), sizeof(void*));
  EXPECT_EQ(observed.block_size() % observed.alignment(), 0u);
  EXPECT_EQ(observed.capacity(), 3u);
  EXPECT_EQ(observed.size(), 0u);
  EXPECT_TRUE(observed.empty());

  void* raw = storage.allocate();
  ASSERT_NE(raw, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(raw) % config.expected_alignment, 0u);
  storage.deallocate(raw);
}

INSTANTIATE_TEST_SUITE_P(
    LegalRequests, MemoryPoolLayout,
    testing::Values(layout_case{0, 1, sizeof(void*), alignof(void*)},
                    layout_case{1, 2, sizeof(void*), alignof(void*)},
                    layout_case{sizeof(void*) - 1, 4, sizeof(void*), alignof(void*)},
                    layout_case{sizeof(void*), alignof(void*), sizeof(void*), alignof(void*)},
                    layout_case{sizeof(void*) + 1, alignof(void*), 2 * sizeof(void*),
                                alignof(void*)},
                    layout_case{16, 16, 16, 16}, layout_case{20, 16, 32, 16},
                    layout_case{0, 64, 64, 64}, layout_case{65, 64, 128, 64}));

class MemoryPoolAlignment : public testing::TestWithParam<size_type> {};

TEST_P(MemoryPoolAlignment, Mem02BlocksAreAlignedUniqueAndDoNotOverlap) {
  memory_pool storage(69, 8, GetParam());
  std::array<void*, 8> blocks{};
  std::set<void*> addresses;
  for (size_type i = 0; i < blocks.size(); ++i) {
    blocks[i] = storage.allocate();
    ASSERT_NE(blocks[i], nullptr);
    ASSERT_TRUE(addresses.insert(blocks[i]).second);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(blocks[i]) % storage.alignment(), 0u);
    std::fill_n(static_cast<std::byte*>(blocks[i]), storage.block_size(),
                std::byte{static_cast<unsigned char>(i + 1)});
  }

  for (size_type i = 0; i < blocks.size(); ++i) {
    const auto* bytes = static_cast<const std::byte*>(blocks[i]);
    EXPECT_TRUE(std::all_of(bytes, bytes + storage.block_size(), [i](std::byte value) {
      return value == std::byte{static_cast<unsigned char>(i + 1)};
    })) << "block " << i;
  }
  EXPECT_EQ(storage.size(), blocks.size());
  EXPECT_EQ(storage.allocate(), nullptr);
  for (void* block : blocks) {
    storage.deallocate(block);
  }
  EXPECT_TRUE(storage.empty());
}

INSTANTIATE_TEST_SUITE_P(SupportedAlignments, MemoryPoolAlignment,
                        testing::Values(1u, 8u, 16u, 64u, 256u));

TEST(MemoryPool, Mem03ZeroCapacityAlwaysReturnsNull) {
  memory_pool storage(0, 0, 1);
  EXPECT_EQ(storage.capacity(), 0u);
  EXPECT_EQ(storage.block_size(), sizeof(void*));
  EXPECT_EQ(storage.alignment(), alignof(void*));
  for (int attempt = 0; attempt < 5; ++attempt) {
    EXPECT_EQ(storage.allocate(), nullptr);
    EXPECT_EQ(storage.size(), 0u);
    EXPECT_TRUE(storage.empty());
  }
}

TEST(MemoryPool, Mem03SingleBlockExhaustsAndRecovers) {
  memory_pool storage(0, 1);
  void* raw = storage.allocate();
  ASSERT_NE(raw, nullptr);
  EXPECT_EQ(storage.size(), 1u);
  EXPECT_FALSE(storage.empty());
  for (int attempt = 0; attempt < 3; ++attempt) {
    EXPECT_EQ(storage.allocate(), nullptr);
    EXPECT_EQ(storage.size(), 1u);
    EXPECT_EQ(storage.capacity(), 1u);
  }
  storage.deallocate(raw);
  EXPECT_EQ(storage.size(), 0u);
  EXPECT_TRUE(storage.empty());
  void* reused = storage.allocate();
  ASSERT_EQ(reused, raw);
  EXPECT_EQ(storage.size(), 1u);
  storage.deallocate(reused);
}

TEST(MemoryPool, Mem03FullCapacityKeepsCountsAfterRepeatedExhaustion) {
  memory_pool storage(24, 7);
  std::array<void*, 7> blocks{};
  for (size_type i = 0; i < blocks.size(); ++i) {
    blocks[i] = storage.allocate();
    ASSERT_NE(blocks[i], nullptr);
    EXPECT_EQ(storage.size(), i + 1);
    EXPECT_EQ(storage.capacity(), blocks.size());
    EXPECT_FALSE(storage.empty());
  }
  for (int attempt = 0; attempt < 5; ++attempt) {
    EXPECT_EQ(storage.allocate(), nullptr);
    EXPECT_EQ(storage.size(), blocks.size());
    EXPECT_EQ(storage.capacity(), blocks.size());
    EXPECT_FALSE(storage.empty());
  }
  for (size_type i = 0; i < blocks.size(); ++i) {
    storage.deallocate(blocks[i]);
    EXPECT_EQ(storage.size(), blocks.size() - i - 1);
  }
  EXPECT_TRUE(storage.empty());
}

TEST(MemoryPool, Mem04ReturnedBlocksAreReusedInReverseReturnOrder) {
  memory_pool storage(32, 4);
  std::array<void*, 4> blocks{};
  for (auto& block : blocks) {
    block = storage.allocate();
    ASSERT_NE(block, nullptr);
  }
  storage.deallocate(blocks[1]);
  storage.deallocate(blocks[3]);
  storage.deallocate(blocks[0]);
  EXPECT_EQ(storage.size(), 1u);

  void* first = storage.allocate();
  void* second = storage.allocate();
  void* third = storage.allocate();
  ASSERT_EQ(first, blocks[0]);
  ASSERT_EQ(second, blocks[3]);
  ASSERT_EQ(third, blocks[1]);
  EXPECT_EQ(storage.size(), 4u);
  EXPECT_EQ(storage.allocate(), nullptr);
  storage.deallocate(first);
  storage.deallocate(second);
  storage.deallocate(third);
  storage.deallocate(blocks[2]);
  EXPECT_TRUE(storage.empty());
}

TEST(MemoryPool, Mem04ReturnedBlockPrecedesUnusedTailWithoutLosingCapacity) {
  memory_pool storage(16, 5);
  void* first = storage.allocate();
  void* second = storage.allocate();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(first, second);
  storage.deallocate(first);

  void* reused = storage.allocate();
  ASSERT_EQ(reused, first);
  std::set<void*> borrowed{second, reused};
  for (int i = 0; i < 3; ++i) {
    void* fresh = storage.allocate();
    ASSERT_NE(fresh, nullptr);
    ASSERT_TRUE(borrowed.insert(fresh).second);
  }
  EXPECT_EQ(storage.size(), 5u);
  EXPECT_EQ(storage.allocate(), nullptr);
  for (void* block : borrowed) {
    storage.deallocate(block);
  }
  EXPECT_TRUE(storage.empty());
}

TEST(MemoryPool, Mem05InterleavedBorrowingMatchesLiveSetAndPreservesLayout) {
  memory_pool storage(35, 11, 16);
  const auto initial_size = storage.block_size();
  const auto initial_alignment = storage.alignment();
  const auto initial_capacity = storage.capacity();
  std::vector<void*> borrowed;
  std::set<void*> live;
  for (size_type step = 0; step < 220; ++step) {
    SCOPED_TRACE(step);
    if (!borrowed.empty() && step % 3 == 0) {
      const size_type index = (step / 3) % borrowed.size();
      void* raw = borrowed[index];
      storage.deallocate(raw);
      EXPECT_EQ(live.erase(raw), 1u);
      borrowed.erase(borrowed.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
      void* raw = storage.allocate();
      if (live.size() == initial_capacity) {
        EXPECT_EQ(raw, nullptr);
      } else {
        ASSERT_NE(raw, nullptr);
        ASSERT_TRUE(live.insert(raw).second);
        borrowed.push_back(raw);
      }
    }
    EXPECT_EQ(storage.size(), live.size());
    EXPECT_EQ(storage.empty(), live.empty());
    EXPECT_EQ(storage.block_size(), initial_size);
    EXPECT_EQ(storage.alignment(), initial_alignment);
    EXPECT_EQ(storage.capacity(), initial_capacity);
  }
  for (void* raw : borrowed) {
    storage.deallocate(raw);
    live.erase(raw);
    EXPECT_EQ(storage.size(), live.size());
  }
  EXPECT_TRUE(storage.empty());

  for (size_type i = 0; i < initial_capacity; ++i) {
    void* raw = storage.allocate();
    ASSERT_NE(raw, nullptr);
    ASSERT_TRUE(live.insert(raw).second);
    EXPECT_EQ(storage.size(), live.size());
  }
  EXPECT_EQ(storage.allocate(), nullptr);
  for (void* raw : live) {
    storage.deallocate(raw);
  }
  EXPECT_TRUE(storage.empty());
}

struct lifecycle_counts {
  int attempts = 0;
  int constructed = 0;
  int destroyed = 0;
  int alive = 0;
};

struct construction_failure {};

struct alignas(64) tracked_object {
  lifecycle_counts& counts;
  std::unique_ptr<int> value;

  tracked_object(lifecycle_counts& observed, int number, bool fail = false) : counts(observed) {
    ++counts.attempts;
    if (fail) {
      throw construction_failure{};
    }
    value = std::make_unique<int>(number);
    ++counts.constructed;
    ++counts.alive;
  }

  ~tracked_object() {
    ++counts.destroyed;
    --counts.alive;
  }
};

TEST(MemoryPool, CallerDestroysNontrivialObjectBeforeReturningStorage) {
  lifecycle_counts counts;
  {
    memory_pool storage(sizeof(tracked_object), 1, alignof(tracked_object));
    EXPECT_EQ(counts.constructed, 0);
    void* raw = storage.allocate();
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(counts.constructed, 0);
    tracked_object* object = nullptr;
    try {
      object = std::construct_at(static_cast<tracked_object*>(raw), counts, 42);
    } catch (...) {
      storage.deallocate(raw);
      throw;
    }
    EXPECT_EQ(*object->value, 42);
    EXPECT_EQ(counts.constructed, 1);
    EXPECT_EQ(counts.alive, 1);
    EXPECT_EQ(storage.allocate(), nullptr);
    std::destroy_at(object);
    EXPECT_EQ(counts.destroyed, 1);
    EXPECT_EQ(counts.alive, 0);
    storage.deallocate(raw);
    EXPECT_EQ(counts.destroyed, 1);
    EXPECT_TRUE(storage.empty());
    void* reused = storage.allocate();
    ASSERT_EQ(reused, raw);
    storage.deallocate(reused);
  }
  EXPECT_EQ(counts.constructed, counts.destroyed);
  EXPECT_EQ(counts.alive, 0);
}

TEST(MemoryPool, CallerReturnsStorageAndRethrowsAfterConstructionFailure) {
  lifecycle_counts counts;
  memory_pool storage(sizeof(tracked_object), 1, alignof(tracked_object));
  void* attempted = nullptr;
  auto construct = [&](bool fail) {
    void* raw = storage.allocate();
    if (raw == nullptr) {
      throw std::bad_alloc{};
    }
    attempted = raw;
    try {
      return std::construct_at(static_cast<tracked_object*>(raw), counts, 73, fail);
    } catch (...) {
      storage.deallocate(raw);
      throw;
    }
  };

  EXPECT_THROW(construct(true), construction_failure);
  EXPECT_EQ(counts.attempts, 1);
  EXPECT_EQ(counts.constructed, 0);
  EXPECT_EQ(counts.destroyed, 0);
  EXPECT_EQ(counts.alive, 0);
  EXPECT_TRUE(storage.empty());
  EXPECT_EQ(storage.size(), 0u);
  void* failed_block = attempted;

  tracked_object* object = construct(false);
  EXPECT_EQ(static_cast<void*>(object), failed_block);
  EXPECT_EQ(*object->value, 73);
  EXPECT_EQ(counts.attempts, 2);
  EXPECT_EQ(counts.constructed, 1);
  EXPECT_EQ(counts.alive, 1);
  EXPECT_EQ(storage.size(), 1u);
  std::destroy_at(object);
  storage.deallocate(object);
  EXPECT_EQ(counts.destroyed, 1);
  EXPECT_EQ(counts.alive, 0);
  EXPECT_TRUE(storage.empty());
}

TEST(MemoryPool, DestructionAcceptsNeverBorrowedStorage) {
  memory_pool storage(48, 8, 64);
  EXPECT_TRUE(storage.empty());
  EXPECT_EQ(storage.capacity(), 8u);
}

TEST(MemoryPool, DestructionAcceptsAllReturnedStorage) {
  memory_pool storage(48, 3, 64);
  std::array<void*, 3> blocks{};
  for (auto& raw : blocks) {
    raw = storage.allocate();
    ASSERT_NE(raw, nullptr);
  }
  for (void* raw : blocks) {
    storage.deallocate(raw);
  }
  EXPECT_TRUE(storage.empty());
}

TEST(MemoryPool, DestructionAcceptsOutstandingRawBlocks) {
  memory_pool storage(48, 3, 64);
  void* first = storage.allocate();
  void* second = storage.allocate();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  std::fill_n(static_cast<std::byte*>(first), storage.block_size(), std::byte{0x31});
  storage.deallocate(second);
  EXPECT_EQ(storage.size(), 1u);
}

TEST(MemoryPool, CallerCanDestroyObjectWithoutReturningItsRawBlock) {
  lifecycle_counts counts;
  {
    memory_pool storage(sizeof(tracked_object), 1, alignof(tracked_object));
    void* raw = storage.allocate();
    ASSERT_NE(raw, nullptr);
    tracked_object* object = nullptr;
    try {
      object = std::construct_at(static_cast<tracked_object*>(raw), counts, 19);
    } catch (...) {
      storage.deallocate(raw);
      throw;
    }
    EXPECT_EQ(*object->value, 19);
    std::destroy_at(object);
    EXPECT_EQ(counts.alive, 0);
    EXPECT_EQ(storage.size(), 1u);
  }
  EXPECT_EQ(counts.constructed, 1);
  EXPECT_EQ(counts.destroyed, 1);
  EXPECT_EQ(counts.alive, 0);
}

}  // namespace
