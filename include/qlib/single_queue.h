#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <utility>

#include <qlib/common.h>

namespace qlib {

// 单生产者、单消费者的有界队列，拥有尚未取出的对象和固定槽位存储。
// T 从 T&& 构造及析构必须实际不抛异常；其他实参构造可以抛异常。
// 生产者调用入队，消费者调用出队；同侧操作不可重入。外部同步后才可交接角色。
// 出队结果独立拥有值。所有访问结束并完成同步（例如 join）后才可销毁队列。
template <typename T>
class single_queue {
 public:
  using value_type = T;
  using size_type = std::size_t;

  // 容量至少为 1，向上取整到 2 的幂；这里只分配内存，不默认构造 T。
  // 调用方保证取整容量可由 size_type、uint64_t 表示且小于 2^64，字节数不溢出。
  explicit single_queue(size_type min_capacity)
      : mask_(std::bit_ceil(min_capacity == 0 ? size_type{1} : min_capacity) - 1),
        slots_(static_cast<T*>(::operator new(capacity() * sizeof(T),
                                             std::align_val_t{alignof(T)}))) {}

  single_queue(const single_queue&) = delete;
  single_queue& operator=(const single_queue&) = delete;

  // 调用方已停止并同步双方，只销毁剩余对象，不再移动对象。
  ~single_queue() {
    const auto tail = tail_.load(std::memory_order_relaxed);
    for (auto head = head_.load(std::memory_order_relaxed); head != tail; ++head) {
      slots_[head & mask_].~T();
    }
    ::operator delete(slots_, std::align_val_t{alignof(T)});
  }

  // 满时不在函数内移动实参或调用 T 的构造函数；实参表达式仍由调用方先求值。
  // 构造失败时不发布新项，原有元素仍可取出，槽位可用于后续重试。
  template <typename... Args>
  bool try_emplace(Args&&... args) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail - cached_head_ > mask_) {
      // 接收消费者的回收发布：槽内旧对象已经读出并销毁，生产者才可复用。
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail - cached_head_ > mask_) {
        return false;
      }
    }
    ::new (static_cast<void*>(slots_ + (tail & mask_))) T(std::forward<Args>(args)...);
    // 发布完整构造的对象，消费者 acquire 接收后才能读取。
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool try_push(T&& value) { return try_emplace(std::move(value)); }

  std::optional<T> try_pop() {
    const auto head = head_.load(std::memory_order_relaxed);
    if (head == cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (head == cached_tail_) {
        return std::nullopt;
      }
    }
    T& slot = slots_[head & mask_];
    std::optional<T> result(std::move(slot));
    slot.~T();
    // 发布回收进度，生产者 acquire 接收后才可覆盖槽位。
    head_.store(head + 1, std::memory_order_release);
    return result;
  }

  size_type capacity() const noexcept { return static_cast<size_type>(mask_ + 1); }

  // 并发期间只供生产者或消费者观察，不是两侧计数的原子快照。
  // 第三方须等访问结束并同步后调用；观察值不能代替入出队结果或停止协议。
  size_type size() const noexcept {
    const auto head = head_.load(std::memory_order_acquire);
    const auto tail = tail_.load(std::memory_order_acquire);
    return static_cast<size_type>(tail - head);
  }

  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

 private:
  // 双方计数的无符号差保持在 [0, capacity()]；掩码负责环形索引。
  const std::uint64_t mask_;
  T* const slots_;
  alignas(QLIB_CACHE_LINE) std::atomic<std::uint64_t> head_{0};  // 消费者写，生产者读。
  std::uint64_t cached_tail_ = 0;  // 仅消费者访问，保存已接收的发布范围。
  alignas(QLIB_CACHE_LINE) std::atomic<std::uint64_t> tail_{0};  // 生产者写，消费者读。
  std::uint64_t cached_head_ = 0;  // 仅生产者访问，保存已接收的回收范围。
};

}  // namespace qlib
