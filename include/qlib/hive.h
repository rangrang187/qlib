#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace qlib {

// 分块拥有 T，插入和删除其他元素保持存活对象的地址及正向迭代器有效。
// 删除、清空和析构使对应对象的指针、引用及迭代器失效。
// 移动赋值先销毁目标原对象；转移或交换保留被接管对象的地址。
// 移动或交换后，调用方须从新所有者重新取得全部迭代器。
// 修改容器后须重新取得反向迭代器；其 base() 保存的是后继位置。
// 构造失败不增加元素数量，但可能保留新分配的空尾块和容量。
// T 的析构须不抛异常；并发写入或读写须由调用方同步。
template <typename T>
class hive {
  using skip_t = std::uint16_t;
  static constexpr skip_t npos = 0xFFFF;
  static constexpr std::size_t min_block_capacity = 8;
  static constexpr std::size_t max_block_capacity = 8192;
  static_assert(max_block_capacity < npos);

  struct free_node {
    skip_t prev;
    skip_t next;
  };

  // 空槽只建立 union 的生命周期；T 由 emplace 和 erase/clear 管理。
  union slot_type {
    T value;
    free_node links;
    slot_type() noexcept {}
    ~slot_type() noexcept {}
  };

  struct block_type {
    block_type* next = nullptr;
    block_type* prev = nullptr;
    block_type* next_erased = nullptr;
    block_type* prev_erased = nullptr;
    slot_type* slots = nullptr;
    skip_t* skipfield = nullptr;
    skip_t free_head = npos;
    std::size_t capacity = 0;
    std::size_t high_water = 0;
    std::size_t active = 0;
  };

  static constexpr std::size_t block_align =
      std::max(alignof(block_type), alignof(slot_type));

  template <bool Const>
  class iterator_impl {
    friend class hive;
    template <bool>
    friend class iterator_impl;
    const hive* owner_ = nullptr;
    block_type* blk_ = nullptr;
    std::size_t idx_ = 0;

    iterator_impl(const hive* owner, block_type* block, std::size_t index) noexcept
        : owner_(owner), blk_(block), idx_(index) {}

   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = std::conditional_t<Const, const T&, T&>;
    using pointer = std::conditional_t<Const, const T*, T*>;

    iterator_impl() noexcept = default;

    template <bool C>
      requires(Const && !C)
    iterator_impl(const iterator_impl<C>& other) noexcept
        : owner_(other.owner_), blk_(other.blk_), idx_(other.idx_) {}

    reference operator*() const noexcept { return blk_->slots[idx_].value; }
    pointer operator->() const noexcept { return std::addressof(blk_->slots[idx_].value); }

    iterator_impl& operator++() noexcept {
      ++idx_;
      if (idx_ < blk_->high_water) idx_ += blk_->skipfield[idx_];
      while (idx_ >= blk_->high_water) {
        blk_ = blk_->next;
        if (blk_ == nullptr) {
          idx_ = 0;
          return *this;
        }
        idx_ = blk_->skipfield[0];
      }
      return *this;
    }

    iterator_impl operator++(int) noexcept {
      auto old = *this;
      ++*this;
      return old;
    }

    iterator_impl& operator--() noexcept {
      if (blk_ == nullptr) {
        blk_ = owner_->tail_;
        idx_ = blk_->high_water;
      }
      for (;;) {
        while (idx_ == 0) {
          blk_ = blk_->prev;
          idx_ = blk_->high_water;
        }
        --idx_;
        const auto run = blk_->skipfield[idx_];
        if (run == 0) return *this;
        if (run <= idx_) {
          idx_ -= run;
          return *this;
        }
        idx_ = 0;
      }
    }

    iterator_impl operator--(int) noexcept {
      auto old = *this;
      --*this;
      return old;
    }

    bool operator==(const iterator_impl& other) const noexcept {
      return blk_ == other.blk_ && idx_ == other.idx_;
    }
  };

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = iterator_impl<false>;
  using const_iterator = iterator_impl<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  hive() noexcept = default;
  hive(const hive&) = delete;
  hive& operator=(const hive&) = delete;

  hive(hive&& other) noexcept { steal(other); }

  hive& operator=(hive&& other) noexcept {
    if (this != std::addressof(other)) {
      clear();
      steal(other);
    }
    return *this;
  }

  ~hive() { clear(); }

  iterator begin() noexcept {
    auto [block, index] = first_position();
    return iterator(this, block, index);
  }

  const_iterator begin() const noexcept {
    auto [block, index] = first_position();
    return const_iterator(this, block, index);
  }

  const_iterator cbegin() const noexcept { return begin(); }
  iterator end() noexcept { return iterator(this, nullptr, 0); }
  const_iterator end() const noexcept { return const_iterator(this, nullptr, 0); }
  const_iterator cend() const noexcept { return end(); }
  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }

  // 返回按 T 大小估算的理论上限；容量运算须处于 size_type 可表示范围内。
  size_type max_size() const noexcept { return static_cast<size_type>(-1) / sizeof(T); }

  iterator insert(T&& value) { return emplace(std::move(value)); }

  template <typename... Args>
  iterator emplace(Args&&... args) {
    if (erased_head_ != nullptr) {
      auto* block = erased_head_;
      const auto index = block->free_head;
      const auto links = block->slots[index].links;
      const auto run = block->skipfield[index];
      try {
        ::new (static_cast<void*>(std::addressof(block->slots[index].value)))
            T(std::forward<Args>(args)...);
      } catch (...) {
        // T 的构造可能覆盖链接字节，失败后重新建立 free_node 的生命周期。
        ::new (static_cast<void*>(std::addressof(block->slots[index].links))) free_node(links);
        throw;
      }
      if (run > 1) {
        const auto new_head = static_cast<skip_t>(index + 1);
        ::new (static_cast<void*>(std::addressof(block->slots[new_head].links)))
            free_node(links);
        block->free_head = new_head;
        if (links.next != npos) block->slots[links.next].links.prev = new_head;
        block->skipfield[new_head] = static_cast<skip_t>(run - 1);
        block->skipfield[index + run - 1] = static_cast<skip_t>(run - 1);
      } else {
        block->free_head = links.next;
        if (links.next != npos) {
          block->slots[links.next].links.prev = npos;
        } else {
          unlink_erased(block);
        }
      }
      block->skipfield[index] = 0;
      ++block->active;
      ++size_;
      return iterator(this, block, index);
    }

    if (tail_ == nullptr || tail_->high_water == tail_->capacity) {
      auto* block = allocate_block(next_block_capacity());
      block->prev = tail_;
      if (tail_ != nullptr) tail_->next = block;
      else head_ = block;
      tail_ = block;
    }
    const auto index = tail_->high_water;
    ::new (static_cast<void*>(std::addressof(tail_->slots[index].value)))
        T(std::forward<Args>(args)...);
    ++tail_->high_water;
    ++tail_->active;
    ++size_;
    return iterator(this, tail_, index);
  }

  // pos 须属于本容器并指向存活对象，返回删除前的后继位置。
  iterator erase(const_iterator pos) {
    auto* block = pos.blk_;
    const auto index = pos.idx_;
    auto next = iterator(this, block, index);
    ++next;
    std::destroy_at(std::addressof(block->slots[index].value));
    --block->active;
    --size_;
    if (block->active == 0) {
      if (block->free_head != npos) unlink_erased(block);
      if (block->prev != nullptr) block->prev->next = block->next;
      else head_ = block->next;
      if (block->next != nullptr) block->next->prev = block->prev;
      else tail_ = block->prev;
      deallocate_block(block);
      return next;
    }

    // 相邻非零标记必为洞段边界；段内旧标记不参与合并判断。
    auto* sf = block->skipfield;
    const auto left = index > 0 ? sf[index - 1] : 0;
    const auto right = index + 1 < block->high_water ? sf[index + 1] : 0;
    if (left == 0 && right == 0) {
      sf[index] = 1;
      push_free(block, index);
    } else if (right == 0) {
      sf[index - left] = sf[index] = static_cast<skip_t>(left + 1);
    } else if (left == 0) {
      sf[index] = sf[index + right] = static_cast<skip_t>(right + 1);
      move_free_node(block, index + 1, index);
    } else {
      sf[index - left] = sf[index + right] = static_cast<skip_t>(left + right + 1);
      remove_free_node(block, index + 1);
    }
    return next;
  }

  // [first,last) 须为本容器的合法正向范围，允许空范围。
  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) first = erase(first);
    return iterator(this, last.blk_, last.idx_);
  }

  // 销毁存活对象并归还全部块；从未构造的槽和删除槽不调用 T 析构。
  void clear() noexcept {
    auto* block = head_;
    while (block != nullptr) {
      auto* next = block->next;
      for (std::size_t index = 0; index < block->high_water;) {
        const auto run = block->skipfield[index];
        if (run == 0) {
          std::destroy_at(std::addressof(block->slots[index].value));
          ++index;
        } else {
          index += run;
        }
      }
      deallocate_block(block);
      block = next;
    }
    head_ = tail_ = erased_head_ = nullptr;
    size_ = 0;
  }

  void swap(hive& other) noexcept {
    using std::swap;
    swap(head_, other.head_);
    swap(tail_, other.tail_);
    swap(erased_head_, other.erased_head_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
  }

  // p 应为本容器存活对象的准确地址；本接口不是任意旧指针的有效性检查。
  iterator get_iterator(const_pointer p) noexcept {
    auto [block, index] = locate(p);
    return iterator(this, block, index);
  }

  const_iterator get_iterator(const_pointer p) const noexcept {
    auto [block, index] = locate(p);
    return const_iterator(this, block, index);
  }

 private:
  std::size_t next_block_capacity() const noexcept {
    return std::clamp(capacity_, min_block_capacity, max_block_capacity);
  }

  block_type* allocate_block(std::size_t cap) {
    const auto offset = (sizeof(block_type) + alignof(slot_type) - 1) /
                        alignof(slot_type) * alignof(slot_type);
    const auto bytes = offset + cap * (sizeof(slot_type) + sizeof(skip_t));
    void* storage = ::operator new(bytes, std::align_val_t(block_align));
    auto* block = ::new (storage) block_type{};
    void* slot_region = static_cast<std::byte*>(storage) + offset;
    // 显式建立整个槽数组，保证槽索引运算指向真实数组中的 union 对象。
    block->slots = ::new (slot_region) slot_type[cap];
    block->skipfield = ::new (static_cast<void*>(block->slots + cap)) skip_t[cap];
    std::memset(block->skipfield, 0, cap * sizeof(skip_t));
    block->capacity = cap;
    capacity_ += cap;
    return block;
  }

  void deallocate_block(block_type* block) noexcept {
    capacity_ -= block->capacity;
    block->~block_type();
    ::operator delete(block, std::align_val_t(block_align));
  }

  std::pair<block_type*, std::size_t> first_position() const noexcept {
    for (auto* block = head_; block != nullptr; block = block->next) {
      const auto index = block->skipfield[0];
      if (index < block->high_water) return {block, index};
    }
    return {nullptr, 0};
  }

  std::pair<block_type*, std::size_t> locate(const_pointer p) const noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(p);
    for (auto* block = head_; block != nullptr; block = block->next) {
      const auto first = reinterpret_cast<std::uintptr_t>(block->slots);
      const auto last = reinterpret_cast<std::uintptr_t>(block->slots + block->high_water);
      if (address >= first && address < last) {
        const auto index = static_cast<std::size_t>(
            reinterpret_cast<const slot_type*>(p) - block->slots);
        if (block->skipfield[index] == 0) return {block, index};
        return {nullptr, 0};
      }
    }
    return {nullptr, 0};
  }

  void push_free(block_type* block, std::size_t index) noexcept {
    const auto old_head = block->free_head;
    ::new (static_cast<void*>(std::addressof(block->slots[index].links)))
        free_node{npos, old_head};
    if (old_head != npos) block->slots[old_head].links.prev = static_cast<skip_t>(index);
    else link_erased(block);
    block->free_head = static_cast<skip_t>(index);
  }

  void move_free_node(block_type* block, std::size_t from, std::size_t to) noexcept {
    const auto links = block->slots[from].links;
    ::new (static_cast<void*>(std::addressof(block->slots[to].links))) free_node(links);
    if (links.prev != npos) block->slots[links.prev].links.next = static_cast<skip_t>(to);
    else block->free_head = static_cast<skip_t>(to);
    if (links.next != npos) block->slots[links.next].links.prev = static_cast<skip_t>(to);
  }

  void remove_free_node(block_type* block, std::size_t index) noexcept {
    const auto links = block->slots[index].links;
    if (links.prev != npos) block->slots[links.prev].links.next = links.next;
    else block->free_head = links.next;
    if (links.next != npos) block->slots[links.next].links.prev = links.prev;
  }

  void link_erased(block_type* block) noexcept {
    block->prev_erased = nullptr;
    block->next_erased = erased_head_;
    if (erased_head_ != nullptr) erased_head_->prev_erased = block;
    erased_head_ = block;
  }

  void unlink_erased(block_type* block) noexcept {
    if (block->prev_erased != nullptr) block->prev_erased->next_erased = block->next_erased;
    else erased_head_ = block->next_erased;
    if (block->next_erased != nullptr) block->next_erased->prev_erased = block->prev_erased;
    block->next_erased = block->prev_erased = nullptr;
  }

  void steal(hive& other) noexcept {
    head_ = std::exchange(other.head_, nullptr);
    tail_ = std::exchange(other.tail_, nullptr);
    erased_head_ = std::exchange(other.erased_head_, nullptr);
    size_ = std::exchange(other.size_, 0);
    capacity_ = std::exchange(other.capacity_, 0);
  }

  block_type* head_ = nullptr;
  block_type* tail_ = nullptr;
  block_type* erased_head_ = nullptr;
  size_type size_ = 0;
  size_type capacity_ = 0;
};

template <typename T>
void swap(hive<T>& a, hive<T>& b) noexcept {
  a.swap(b);
}

}  // namespace qlib
