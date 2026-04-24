#pragma once

/**
 * @file client_handler.h
 * @brief ClientHandler — lean per-connection I/O orchestrator (Module 24).
 */

#include "executor/connection_job.h"

#include <chrono>
#include <optional>

namespace mqtt {

class Broker;
class ConnectionSession;
class ConnectionTable;
class IoReactor;
class JobScheduler;
struct BrokerConfig;

namespace client_handler {

void process_accept_job(const AcceptJobPayload &payload, ConnectionTable &table,
                        IoReactor &reactor, JobScheduler &scheduler,
                        Broker &broker, const BrokerConfig &config);

void process_decode_job(int fd, ConnectionTable &table, IoReactor &reactor,
                        JobScheduler &scheduler, Broker &broker);

void process_drain_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       JobScheduler &scheduler, Broker &broker);

void process_close_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       Broker &broker);

[[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
next_decode_deadline(ConnectionSession &session);

} // namespace client_handler

} // namespace mqtt
