#pragma once
#include "zensim/ZpcFunction.hpp"
#include "zensim/execution/Concurrency.h"
#include "zensim/execution/ManagedThread.hpp"

namespace zs {

  struct IO {
  private:
    void wait() {
      std::unique_lock<std::mutex> lk{mut};
      cv.wait(lk, [this]() { return !this->bRunning || !this->jobs.empty(); });
    };
    void worker() {
      while (bRunning) {
        wait();
        auto job = jobs.try_pop();
        if (job) (*job)();
      }
    }
    IO() : bRunning{true} {
      th.start([this](ManagedThread &) { worker(); }, "io-worker");
    }

  public:
    ZPC_BACKEND_API static IO &instance() {
      static IO s_instance{};
      return s_instance;
    }
    ~IO() {
      while (!jobs.empty()) cv.notify_all();
      bRunning = false;
      if (th.joinable()) th.join();
    }

    static void flush() {
      while (!instance().jobs.empty()) instance().cv.notify_all();
    }
    static void insert_job(zs::function<void()> job) {
      std::unique_lock<std::mutex> lk{instance().mut};
      instance().jobs.push(job);
      lk.unlock();
      instance().cv.notify_all();
    }

  private:
    bool bRunning;
    std::mutex mut;
    std::condition_variable cv;
    threadsafe_queue<zs::function<void()>> jobs;
    ManagedThread th;
  };

  std::string file_get_content(std::string const &path);
  void *load_raw_file(char const *filename, size_t size);

}  // namespace zs
