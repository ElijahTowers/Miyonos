#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace miyonos {

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  bool try_push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(value));
    cv_.notify_one();
    return true;
  }

  template <typename Predicate>
  bool replace_latest(Predicate predicate, T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return false;
    for (auto it = queue_.rbegin(); it != queue_.rend(); ++it) {
      if (predicate(*it)) {
        *it = std::move(value);
        cv_.notify_one();
        return true;
      }
    }
    if (queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(value));
    cv_.notify_one();
    return true;
  }

  bool wait_pop(T& value, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                 [&] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) return false;
    value = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  bool try_pop(T& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    value = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<T> queue_;
  bool closed_ = false;
};

}  // namespace miyonos
