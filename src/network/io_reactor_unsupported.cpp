#if !defined(__linux__) && !defined(__APPLE__) && !defined(__FreeBSD__) &&     \
  !defined(__OpenBSD__) && !defined(__NetBSD__) && !defined(_WIN32)

#include "network/io_reactor.h"

#include <stdexcept>

namespace mqtt {

IoReactor::IoReactor(StructuredTracer *tracer) : tracer_(tracer) {}

IoReactor::~IoReactor() { stop(); }

void IoReactor::start() {
  throw std::runtime_error(
  "IoReactor is unsupported on this platform (requires epoll, kqueue, or WSAPoll)");
}

void IoReactor::stop() noexcept {}

void IoReactor::register_listener(int socket_fd, AcceptCallback callback) {
  (void)socket_fd;
  (void)callback;
}

void IoReactor::register_connection(int socket_fd, ReadCallback read_callback,
                                    WriteCallback write_callback) {
  (void)socket_fd;
  (void)read_callback;
  (void)write_callback;
}

void IoReactor::arm_write(int socket_fd) { (void)socket_fd; }

void IoReactor::disarm_write(int socket_fd) { (void)socket_fd; }

void IoReactor::unregister(int socket_fd) { (void)socket_fd; }

void IoReactor::wake() noexcept {}

void IoReactor::run_loop() noexcept {}

} // namespace mqtt

#endif

