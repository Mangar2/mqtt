#pragma once

/**
 * @file client_handler.h
 * @brief ClientHandler — lean per-connection I/O orchestrator (Module 24).
 */

#include "executor/connection_job.h"

namespace mqtt {

class Broker;
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

} // namespace client_handler

} // namespace mqtt
