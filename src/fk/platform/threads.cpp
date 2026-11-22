// Worker pool used to compose a frame in horizontal bands on several cores.
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../Backend.h"
#include "../Platform.h"
#if defined(ESP_PLATFORM)
#include "esp_pthread.h"
#include "soc/soc_caps.h"
#endif

namespace fk {
namespace platform {

int cpuCount() {
#if defined(ESP_PLATFORM)
  return SOC_CPU_CORES_NUM;
#else
  int n = (int)std::thread::hardware_concurrency();
  return n > 0 ? n : 1;
#endif
}

int defaultRenderThreads() {
#if defined(FK_IS_ESP_LCD) || defined(FK_IS_M5)
  return 1;  // opt in on ESP32 with Config::renderThreads = 2 (the P4 has two cores)
#else
  return std::min(4, cpuCount());
#endif
}

namespace {
class Pool {
 public:
  Pool() {
    int n = cpuCount() - 1;
#if defined(ESP_PLATFORM)
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 6 * 1024;
    cfg.thread_name = "fk-render";
    esp_pthread_set_cfg(&cfg);
#endif
    for (int i = 0; i < n; i++) workers_.emplace_back([this] { loop(); });
  }
  ~Pool() {
    { std::lock_guard<std::mutex> lk(m_); stop_ = true; }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
  }
  void run(int n, const std::function<void(int)>& f) {
    if (n <= 1 || workers_.empty()) { for (int i = 0; i < n; i++) f(i); return; }
    {
      std::lock_guard<std::mutex> lk(m_);
      job_ = &f; total_ = n; next_ = 0; remaining_ = n;
    }
    cv_.notify_all();
    work();  // the calling thread works too
    std::unique_lock<std::mutex> lk(m_);
    done_.wait(lk, [&] { return remaining_ == 0; });
    job_ = nullptr;
  }

 private:
  void work() {
    for (;;) {
      int i;
      { std::lock_guard<std::mutex> lk(m_); if (!job_ || next_ >= total_) return; i = next_++; }
      (*job_)(i);
      { std::lock_guard<std::mutex> lk(m_); if (--remaining_ == 0) done_.notify_all(); }
    }
  }
  void loop() {
    for (;;) {
      { std::unique_lock<std::mutex> lk(m_); cv_.wait(lk, [&] { return stop_ || (job_ && next_ < total_); }); if (stop_) return; }
      work();
    }
  }
  std::vector<std::thread> workers_;
  std::mutex m_;
  std::condition_variable cv_, done_;
  const std::function<void(int)>* job_ = nullptr;
  int total_ = 0, next_ = 0, remaining_ = 0;
  bool stop_ = false;
};
}  // namespace

void parallelFor(int count, const std::function<void(int)>& fn) {
  static Pool pool;
  pool.run(count, fn);
}

}  // namespace platform
}  // namespace fk
