#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace qlib {

// 拥有键值对和单块槽位内存，以线性探测维护唯一键映射。
// 只允许修改 second；即使 first 的类型可写，也不得修改已存储的键。
// 插入调用或重哈希后须重新取得迭代器和引用；删除只使被删项的位置失效。
// clear 和析构使所有位置失效；移动和交换改变存储归属，须重新取得位置。
// 重哈希中的 pair 移动、析构、哈希、比较和实际使用的策略操作必须不抛异常。
// 策略须可默认构造，等价键须有相同哈希，存储期间哈希和等价规则须保持稳定。
// 新项构造可抛异常；失败不增加活项，但此前完成的重哈希及参数副作用会保留。
// 插入参数不得引用本表中可能被重哈希搬走的对象；需要时先在表外暂存。
// 容量取整、4*n+2、墓碑预算、容量翻倍和分配字节数均须可表示，调用方负责保证。
// 修改、移动、交换和销毁需要独占访问；并发只读还要求值与策略没有数据竞争。
// 用户构造和策略调用不得重入本表；裸指针值所指对象仍由调用方管理。
template <typename Key, typename T, typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class map {
 private:
  template <bool IsConst>
  class iterator_impl {
    friend class map;
    template <bool>
    friend class iterator_impl;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::pair<Key, T>;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;
    using reference = std::conditional_t<IsConst, const value_type&, value_type&>;

    iterator_impl() noexcept = default;

    template <bool C>
      requires(IsConst && !C)
    iterator_impl(const iterator_impl<C>& other) noexcept
        : ctrl_(other.ctrl_), slot_(other.slot_) {}

    reference operator*() const noexcept { return *slot_; }
    pointer operator->() const noexcept { return slot_; }

    iterator_impl& operator++() noexcept {
      ++ctrl_;
      ++slot_;
      skip();
      return *this;
    }

    iterator_impl operator++(int) noexcept {
      auto old = *this;
      ++*this;
      return old;
    }

    bool operator==(const iterator_impl&) const noexcept = default;

   private:
    iterator_impl(std::uint8_t* ctrl, value_type* slot) noexcept : ctrl_(ctrl), slot_(slot) {}

    // 哨兵最高位为零，使顺序扫描停在尾后；尾后没有可解引用的 pair。
    void skip() noexcept {
      while (*ctrl_ & 0x80) {
        ++ctrl_;
        ++slot_;
      }
    }

    std::uint8_t* ctrl_ = nullptr;
    value_type* slot_ = nullptr;
  };

 public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = std::pair<Key, T>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using iterator = iterator_impl<false>;
  using const_iterator = iterator_impl<true>;

  map() noexcept = default;
  map(const map&) = delete;
  map& operator=(const map&) = delete;

  map(map&& other) noexcept { steal(other); }

  map& operator=(map&& other) noexcept {
    if (this != &other) {
      clear();
      steal(other);
    }
    return *this;
  }

  ~map() { clear(); }

  iterator begin() noexcept {
    if (size_ == 0) return end();
    iterator result(ctrl_, slots_);
    result.skip();
    return result;
  }

  const_iterator begin() const noexcept {
    if (size_ == 0) return end();
    const_iterator result(ctrl_, slots_);
    result.skip();
    return result;
  }

  const_iterator cbegin() const noexcept { return begin(); }

  iterator end() noexcept {
    if (capacity_ == 0) return {};
    return iterator(ctrl_ + capacity_, slots_ + capacity_);
  }

  const_iterator end() const noexcept {
    if (capacity_ == 0) return {};
    return const_iterator(ctrl_ + capacity_, slots_ + capacity_);
  }

  const_iterator cend() const noexcept { return end(); }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }

  // 返回未扣除控制区的参考上界，不能作为容量输入的安全上限。
  size_type max_size() const noexcept { return size_type(-1) / sizeof(value_type); }

  // 为总计 n 项预留空间；已有墓碑也占预算，整理可能改变容量及所有位置。
  void reserve(size_type n) {
    if (n < size_) n = size_;
    if (n + (used_ - size_) <= max_load()) return;
    rehash_to(capacity_for(n));
  }

  iterator find(const Key& key) noexcept {
    const auto idx = find_index(key);
    return idx == npos ? end() : make_iter(idx);
  }

  const_iterator find(const Key& key) const noexcept {
    const auto idx = find_index(key);
    return idx == npos ? end() : const_iterator(ctrl_ + idx, slots_ + idx);
  }

  bool contains(const Key& key) const noexcept { return find_index(key) != npos; }

  T& at(const Key& key) {
    const auto idx = find_index(key);
    if (idx == npos) throw std::out_of_range("qlib::map::at");
    return slots_[idx].second;
  }

  const T& at(const Key& key) const {
    const auto idx = find_index(key);
    if (idx == npos) throw std::out_of_range("qlib::map::at");
    return slots_[idx].second;
  }

  std::pair<iterator, bool> insert(value_type&& value) {
    return insert_key(value.first, [&](value_type* slot) {
      ::new (static_cast<void*>(slot)) value_type(std::move(value));
    });
  }

  // 临时 pair 在查重前构造；重复键也可能已移动实参。
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return insert(value_type(std::forward<Args>(args)...));
  }

  // 重复键返回已有项，函数体不移动 key，也不转发值的构造参数。
  template <typename... Args>
  std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
    return insert_key(key, [&](value_type* slot) {
      ::new (static_cast<void*>(slot)) value_type(
          std::piecewise_construct, std::forward_as_tuple(std::move(key)),
          std::forward_as_tuple(std::forward<Args>(args)...));
    });
  }

  T& operator[](Key&& key) { return try_emplace(std::move(key)).first->second; }

  // pos 必须是本表的有效活项位置；返回按槽位顺序前进的后继。
  iterator erase(const_iterator pos) noexcept {
    const auto idx = static_cast<size_type>(pos.ctrl_ - ctrl_);
    slots_[idx].~value_type();
    --size_;
    if (ctrl_[(idx + 1) & (capacity_ - 1)] == ctrl_empty) {
      ctrl_[idx] = ctrl_empty;
      --used_;
    } else {
      ctrl_[idx] = ctrl_deleted;
    }
    auto result = make_iter(idx);
    result.skip();
    return result;
  }

  size_type erase(const Key& key) noexcept {
    const auto idx = find_index(key);
    if (idx == npos) return 0;
    erase(const_iterator(ctrl_ + idx, slots_ + idx));
    return 1;
  }

  // 销毁活项并释放整块存储；控制区和哨兵不拥有独立分配。
  void clear() noexcept {
    for (size_type idx = 0; idx < capacity_; ++idx) {
      if (!(ctrl_[idx] & 0x80)) slots_[idx].~value_type();
    }
    if (capacity_ != 0) ::operator delete(slots_, std::align_val_t(alignof(value_type)));
    slots_ = nullptr;
    ctrl_ = nullptr;
    capacity_ = size_ = used_ = 0;
  }

  void swap(map& other) noexcept {
    if (this == &other) return;
    std::swap(slots_, other.slots_);
    std::swap(ctrl_, other.ctrl_);
    std::swap(capacity_, other.capacity_);
    std::swap(size_, other.size_);
    std::swap(used_, other.used_);
    std::swap(hash_, other.hash_);
    std::swap(eq_, other.eq_);
  }

 private:
  static constexpr std::uint8_t ctrl_empty = 0x80;
  static constexpr std::uint8_t ctrl_deleted = 0x81;
  static constexpr size_type min_capacity = 8;
  static constexpr size_type npos = size_type(-1);

  static std::uint64_t mix(std::size_t hash) noexcept {
    std::uint64_t value = hash;
    value ^= value >> 33;
    value *= 0xFF51AFD7ED558CCDULL;
    value ^= value >> 33;
    value *= 0xC4CEB9FE1A85EC53ULL;
    value ^= value >> 33;
    return value;
  }

  static std::uint8_t fragment(std::uint64_t hash) noexcept {
    return static_cast<std::uint8_t>(hash >> 57);
  }

  size_type max_load() const noexcept { return capacity_ - capacity_ / 4; }

  static size_type capacity_for(size_type n) noexcept {
    const auto rounded = std::bit_ceil((4 * n + 2) / 3);
    return rounded < min_capacity ? min_capacity : rounded;
  }

  // 墓碑只记录候选位置；遇到 empty 后才能断定不存在相同键。
  size_type locate(const Key& key, std::uint64_t hash, std::uint8_t h2,
                   size_type* slot) const noexcept {
    const auto mask = capacity_ - 1;
    auto idx = static_cast<size_type>(hash) & mask;
    auto first_deleted = npos;
    for (;;) {
      if (ctrl_[idx] == h2 && eq_(slots_[idx].first, key)) return idx;
      if (ctrl_[idx] == ctrl_empty) {
        *slot = first_deleted == npos ? idx : first_deleted;
        return npos;
      }
      if (ctrl_[idx] == ctrl_deleted && first_deleted == npos) first_deleted = idx;
      idx = (idx + 1) & mask;
    }
  }

  size_type free_slot(std::uint64_t hash) const noexcept {
    const auto mask = capacity_ - 1;
    auto idx = static_cast<size_type>(hash) & mask;
    while (ctrl_[idx] != ctrl_empty) idx = (idx + 1) & mask;
    return idx;
  }

  size_type find_index(const Key& key) const noexcept {
    if (size_ == 0) return npos;
    const auto hash = mix(hash_(key));
    size_type unused;
    return locate(key, hash, fragment(hash), &unused);
  }

  iterator make_iter(size_type idx) noexcept { return iterator(ctrl_ + idx, slots_ + idx); }

  template <typename Construct>
  std::pair<iterator, bool> insert_key(const Key& key, Construct construct) {
    const auto hash = mix(hash_(key));
    const auto h2 = fragment(hash);
    size_type slot = 0;
    bool into_empty = true;
    if (capacity_ != 0) {
      const auto found = locate(key, hash, h2, &slot);
      if (found != npos) return {make_iter(found), false};
      into_empty = ctrl_[slot] == ctrl_empty;
    }
    if (into_empty && used_ + 1 > max_load()) {
      const auto next_capacity = capacity_ == 0 ? min_capacity
          : size_ >= capacity_ / 2 ? 2 * capacity_ : capacity_;
      rehash_to(next_capacity);
      slot = free_slot(hash);
    }
    // 构造失败时保留原控制字节与计数，避免暴露尚未存活的 pair。
    construct(slots_ + slot);
    ctrl_[slot] = h2;
    used_ += into_empty;
    ++size_;
    return {make_iter(slot), true};
  }

  void allocate(size_type cap) {
    auto* block = static_cast<value_type*>(::operator new(
        cap * sizeof(value_type) + cap + 1, std::align_val_t(alignof(value_type))));
    slots_ = block;
    ctrl_ = reinterpret_cast<std::uint8_t*>(block) + cap * sizeof(value_type);
    std::memset(ctrl_, ctrl_empty, cap);
    ctrl_[cap] = 0;
    capacity_ = cap;
  }

  // 分配成功后切换存储，再以不抛迁移搬入活项；墓碑不迁移。
  void rehash_to(size_type new_cap) {
    auto* old_slots = slots_;
    auto* old_ctrl = ctrl_;
    const auto old_cap = capacity_;
    allocate(new_cap);
    used_ = size_;
    for (size_type idx = 0; idx < old_cap; ++idx) {
      if (old_ctrl[idx] & 0x80) continue;
      const auto hash = mix(hash_(old_slots[idx].first));
      const auto slot = free_slot(hash);
      ::new (static_cast<void*>(slots_ + slot)) value_type(std::move(old_slots[idx]));
      ctrl_[slot] = fragment(hash);
      old_slots[idx].~value_type();
    }
    if (old_cap != 0) ::operator delete(old_slots, std::align_val_t(alignof(value_type)));
  }

  void steal(map& other) noexcept {
    slots_ = std::exchange(other.slots_, nullptr);
    ctrl_ = std::exchange(other.ctrl_, nullptr);
    capacity_ = std::exchange(other.capacity_, 0);
    size_ = std::exchange(other.size_, 0);
    used_ = std::exchange(other.used_, 0);
    hash_ = std::move(other.hash_);
    eq_ = std::move(other.eq_);
  }

  value_type* slots_ = nullptr;
  std::uint8_t* ctrl_ = nullptr;
  size_type capacity_ = 0;
  size_type size_ = 0;
  size_type used_ = 0;
  [[no_unique_address]] Hash hash_{};
  [[no_unique_address]] KeyEqual eq_{};
};

template <typename Key, typename T, typename Hash, typename KeyEqual>
void swap(map<Key, T, Hash, KeyEqual>& a, map<Key, T, Hash, KeyEqual>& b) noexcept {
  a.swap(b);
}

}  // namespace qlib
