#include "network/write_queue.h"

#include <cstddef>
#include <mutex>
#include <vector>

#include "network/tcp_connection.h"

namespace mqtt {

WriteQueue::WriteQueue(std::size_t max_bytes) : max_bytes_(max_bytes) {}

bool WriteQueue::enqueue(std::vector<uint8_t> packet) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queued_bytes_ + packet.size() > max_bytes_) {
    return false;
  }
  queued_bytes_ += packet.size();
  queue_.push(std::move(packet));
  cv_.notify_one();
  return true;
}

bool WriteQueue::drain(TcpConnection &conn) {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!queue_.empty()) {
    std::vector<uint8_t> pkt = std::move(queue_.front());
    queue_.pop();
    queued_bytes_ -= pkt.size();
    lock.unlock();
    if (!conn.write(pkt)) {
      return false;
    }
    lock.lock();
  }
  return true;
}

void WriteQueue::run_drain(TcpConnection &conn) {
  while (true) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || stopped_.load(); });

    if (stopped_.load() && queue_.empty()) {
      break;
    }

    while (!queue_.empty()) {
      std::vector<uint8_t> pkt = std::move(queue_.front());
      queue_.pop();
      queued_bytes_ -= pkt.size();
      lock.unlock();
      if (!conn.write(pkt)) {
        return; // socket error — exit drain loop
      }
      lock.lock();
    }
  }
}

void WriteQueue::stop() {
  stopped_.store(true);
  cv_.notify_all();
}

bool WriteQueue::is_full() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return queued_bytes_ >= max_bytes_;
}

bool WriteQueue::is_empty() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

std::size_t WriteQueue::queued_bytes() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return queued_bytes_;
}

} // namespace mqtt
