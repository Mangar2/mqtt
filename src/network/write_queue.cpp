#include "network/write_queue.h"

#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

#include "monitoring/structured_tracer.h"
#include "network/tcp_connection.h"

namespace mqtt {

namespace {

void emit_write_queue_trace(StructuredTracer *tracer, std::string_view queue_id,
                            std::string_view info,
                            std::size_t frame_bytes,
                            std::size_t queued_bytes,
                            std::size_t max_bytes) {
  TRACE_GUARD(tracer, TraceLevel::Trace, "connection") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "connection";
    event.info = std::string(info);
    event.data.emplace_back("queue_id", std::string(queue_id));
    event.data.emplace_back("frame_bytes", std::to_string(frame_bytes));
    event.data.emplace_back("queued_bytes", std::to_string(queued_bytes));
    event.data.emplace_back("max_bytes", std::to_string(max_bytes));
    tracer->emit(event);
  }
}

} // namespace

WriteQueue::WriteQueue(std::size_t max_bytes) : max_bytes_(max_bytes) {}

void WriteQueue::set_tracer(StructuredTracer *tracer,
                            std::string queue_id) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  structured_tracer_ = tracer;
  queue_id_ = std::move(queue_id);
}

bool WriteQueue::enqueue(std::vector<uint8_t> packet) {
  const std::size_t frame_bytes = packet.size();
  std::lock_guard<std::mutex> lock(mutex_);
  if (queued_bytes_ + frame_bytes > max_bytes_) {
    emit_write_queue_trace(structured_tracer_, queue_id_,
                           "write_queue_enqueue_rejected", frame_bytes,
                           queued_bytes_, max_bytes_);
    return false;
  }
  queued_bytes_ += frame_bytes;
  queue_.push(std::move(packet));
  emit_write_queue_trace(structured_tracer_, queue_id_,
                         "write_queue_enqueue_accepted", frame_bytes,
                         queued_bytes_, max_bytes_);
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
      emit_write_queue_trace(structured_tracer_, queue_id_,
                             "write_queue_drain_write_failed", pkt.size(),
                             queued_bytes_, max_bytes_);
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
        emit_write_queue_trace(structured_tracer_, queue_id_,
                               "write_queue_drain_write_failed", pkt.size(),
                               queued_bytes_, max_bytes_);
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
