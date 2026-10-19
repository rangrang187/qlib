#pragma once

#include <cstddef>
#include <cstring>
#include <new>

namespace qlib {

// 拥有固定容量的原始内存，借出地址在归还或池销毁前保持稳定。
// 调用方负责构造对象，并在归还或池销毁前完成对象清理；池只释放底层缓冲区。
// 构造可能传播分配异常；借还和查询不抛异常，借还期间池自身不新增分配。
// 无写入时允许并发只读；借还、查询与销毁之间的冲突访问由调用方同步。
class memory_pool {
 public:
  using size_type = std::size_t;

  // alignment 必须为非零二次幂，实际对齐 a=max(alignment,alignof(void*)) 须受平台支持。
  // 令 n=max(block_size,sizeof(void*))、M=size_type 最大值，要求 n<=M-(a-1)。
  // 取整得到步长 b 后，要求 capacity<=M/b，且分配器能表示总字节数。
  // 零块大小和零容量均合法；零容量仍调用一次对齐分配，也可能抛出分配异常。
  memory_pool(size_type block_size, size_type capacity,
              size_type alignment = alignof(std::max_align_t))
      : align_(alignment < alignof(void*) ? alignof(void*) : alignment),
        block_size_(round_up(block_size < sizeof(void*) ? sizeof(void*) : block_size, align_)),
        capacity_(capacity),
        data_(static_cast<std::byte*>(
            ::operator new(capacity_ * block_size_, std::align_val_t{align_}))) {}

  memory_pool(const memory_pool&) = delete;
  memory_pool& operator=(const memory_pool&) = delete;

  // 允许仍有原始块未归还；调用方须先结束块内对象的生命周期和全部访问。
  ~memory_pool() { ::operator delete(data_, std::align_val_t{align_}); }

  size_type block_size() const noexcept { return block_size_; }
  size_type alignment() const noexcept { return align_; }

  // 返回当前是否没有借出块；有空闲空间的池也可能为空。
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }

  // 借出未初始化内存，优先复用最近归还的块；耗尽返回 nullptr，计数保持不变。
  void* allocate() noexcept {
    if (free_head_ != nullptr) {
      void* result = free_head_;
      std::memcpy(&free_head_, result, sizeof(void*));
      ++size_;
      return result;
    }
    if (used_ == capacity_) {
      return nullptr;
    }
    void* result = data_ + used_ * block_size_;
    ++used_;
    ++size_;
    return result;
  }

  // p 必须是本池尚未归还的非空块起点；调用方须先销毁块内对象并停止访问。
  // 复制真实 void* 的表示，保留平台的空指针表示，并避免把块头当作指针对象访问。
  void deallocate(void* p) noexcept {
    std::memcpy(p, &free_head_, sizeof(void*));
    free_head_ = p;
    --size_;
  }

 private:
  static constexpr size_type round_up(size_type n, size_type a) noexcept {
    return (n + a - 1) / a * a;
  }

  const size_type align_;
  const size_type block_size_;
  const size_type capacity_;
  std::byte* const data_;
  void* free_head_ = nullptr;
  // used_ 只在首次借出尾部块时增加；size_ 加空闲链长度始终等于 used_。
  size_type used_ = 0;
  size_type size_ = 0;
};

}  // namespace qlib
