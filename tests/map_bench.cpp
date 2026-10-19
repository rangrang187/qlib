#include <qlib/map.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

namespace {

constexpr int n = 1'000'000;
constexpr int reps = 3;

// 输出累计结果，使查询和删除的实际结果参与可观察行为。
std::uint64_t sink = 0;

template <typename F>
double timed(F&& f) {
  const auto start = std::chrono::steady_clock::now();
  f();
  const auto stop = std::chrono::steady_clock::now();
  return static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()) / n;
}

template <typename Map>
void run(const char* name, const std::vector<int>& keys, const std::vector<int>& miss) {
  double insert = 1e300;
  double find_hit = 1e300;
  double find_miss = 1e300;
  double erase = 1e300;

  // 查询共用预填映射，使三轮查询从同一组键和值开始。
  Map full;
  for (int k : keys) full.try_emplace(int{k}, k);

  for (int rep = 0; rep < reps; ++rep) {
    {
      // 默认构造与析构留在计时外，插入触发的增长计入耗时。
      Map m;
      insert = std::min(insert, timed([&] {
        for (int k : keys) m.try_emplace(int{k}, k);
      }));
      sink += m.size();
    }

    find_hit = std::min(find_hit, timed([&] {
      for (int k : keys) sink += static_cast<std::uint64_t>(full.find(k)->second);
    }));

    find_miss = std::min(find_miss, timed([&] {
      for (int k : miss) sink += full.contains(k);
    }));

    {
      // 每轮单独预填删除用映射，使删除计时只覆盖实际删除与累计。
      Map m;
      for (int k : keys) m.try_emplace(int{k}, k);
      erase = std::min(erase, timed([&] {
        for (int k : keys) sink += m.erase(k);
      }));
    }
  }

  std::printf("%s,insert,%.1f\n", name, insert);
  std::printf("%s,find_hit,%.1f\n", name, find_hit);
  std::printf("%s,find_miss,%.1f\n", name, find_miss);
  std::printf("%s,erase,%.1f\n", name, erase);
}

}  // namespace

int main() {
  std::vector<int> keys(n);
  std::iota(keys.begin(), keys.end(), 0);
  std::vector<int> miss(n);
  std::iota(miss.begin(), miss.end(), n);

  std::mt19937 rng(42);
  std::shuffle(keys.begin(), keys.end(), rng);
  std::shuffle(miss.begin(), miss.end(), rng);

  run<qlib::map<int, int>>("qlib::map", keys, miss);
  run<std::unordered_map<int, int>>("std::unordered_map", keys, miss);
  std::fprintf(stderr, "sink=%llu\n", static_cast<unsigned long long>(sink));
  return 0;
}
