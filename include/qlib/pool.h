#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace qlib {

// 拥有池内对象；release 或池析构结束对象生命周期，最近释放的槽位优先复用。
// 句柄只对本池当前存活对象有效；释放后的旧句柄不得用于访问或再次释放。
// 增长保留有效句柄，但可能使全部旧指针和引用失效；操作后应按句柄重新访问。
// T 的移动构造和析构须实际不抛异常；指定参数的普通构造可以抛异常。
// 新对象构造失败保留原存活数量；已完成的增长及其地址变化可以保留。
// 可能增长时，构造参数须在池外保存，避免参数引用被搬迁对象或其成员。
// 容量及增长计算须可表示；预留和增长中的标准库异常直接传给调用方。
// 无写入时允许并发只读；修改和销毁需独占访问，用户代码不得重入本池。
template <typename T>
class pool {
 private:
  struct slot_type {
    union {
      std::size_t next_free;
      T value;
    };
    bool live;

    slot_type() noexcept : next_free(0), live(false) {}

    slot_type(slot_type&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : live(other.live) {
      if (live) {
        ::new (static_cast<void*>(std::addressof(value))) T(std::move(other.value));
        // 源对象仍需析构；vector 清理旧槽时依靠源 live 保留其生命周期信息。
      } else {
        next_free = other.next_free;
      }
    }

    ~slot_type() {
      if (live) value.~T();
    }
  };

 public:
  using value_type = T;
  using size_type = std::size_t;

  // 只预留槽位存储；请求容量允许为零，且不限制后续对象数量。
  explicit pool(size_type capacity) { slots_.reserve(capacity); }

  pool(const pool&) = delete;
  pool& operator=(const pool&) = delete;

  T& operator[](size_type handle) noexcept { return slots_[handle].value; }
  const T& operator[](size_type handle) const noexcept { return slots_[handle].value; }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return slots_.capacity(); }

  template <typename... Args>
  size_type emplace(Args&&... args) {
    if (free_head_ != npos) {
      const auto handle = free_head_;
      auto& slot = slots_[handle];
      const auto next = slot.next_free;
      try {
        ::new (static_cast<void*>(std::addressof(slot.value))) T(std::forward<Args>(args)...);
      } catch (...) {
        // 失败构造也可能覆盖 union；写回后继才能保留完整空闲链。
        slot.next_free = next;
        throw;
      }
      slot.live = true;
      free_head_ = next;
      ++size_;
      return handle;
    }

    slots_.resize(slots_.size() + 1);
    auto& slot = slots_.back();
    try {
      ::new (static_cast<void*>(std::addressof(slot.value))) T(std::forward<Args>(args)...);
    } catch (...) {
      // 临时末槽仍为空；移除它保留已完成的扩容，并跳过失败对象的析构。
      slots_.pop_back();
      throw;
    }
    slot.live = true;
    ++size_;
    return slots_.size() - 1;
  }

  // handle 须指向本池当前存活对象；释放保留槽位容量。
  void release(size_type handle) noexcept {
    auto& slot = slots_[handle];
    slot.value.~T();
    slot.live = false;
    slot.next_free = free_head_;
    free_head_ = handle;
    --size_;
  }

 private:
  static constexpr size_type npos = static_cast<size_type>(-1);

  std::vector<slot_type> slots_;
  size_type free_head_ = npos;
  size_type size_ = 0;
};

}  // namespace qlib
