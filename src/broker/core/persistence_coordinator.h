#pragma once

/**
 * @file persistence_coordinator.h
 * @brief Persistence load/flush helper extracted from Broker.
 */

#include "broker/message_router/offline_queue.h"
#include "broker/persistence/inflight_persistence.h"
#include "broker/persistence/offline_queue_persistence.h"
#include "broker/persistence/retained_message_persistence.h"
#include "broker/persistence/session_persistence.h"
#include "broker/store/inflight_store.h"
#include "broker/store/retained_message_store.h"
#include "broker/store/session_store.h"
#include "broker/store/subscription_store.h"

namespace mqtt {

/**
 * @brief Coordinates persistence load/flush over store and adapter objects.
 */
class PersistenceCoordinator {
public:
  /**
   * @brief Restore persisted sessions, retained messages, inflight entries,
   *        and offline queue contents.
   */
  static void load(SessionPersistence &session_persistence,
                   RetainedMessagePersistence &retained_persistence,
                   InflightPersistence &inflight_persistence,
                   OfflineQueuePersistence &offline_queue_persistence,
                   bool include_inflight_states,
                   SessionStore &session_store,
                   RetainedMessageStore &retained_store,
                   SubscriptionStore &subscription_store,
                   InflightStore &inflight_store,
                   OfflineQueue &offline_queue);

  /**
   * @brief Persist current sessions, retained messages, inflight entries,
   *        and offline queue contents.
   *
   * This method is noexcept by design for shutdown paths.
   */
  static void flush(SessionPersistence &session_persistence,
                    RetainedMessagePersistence &retained_persistence,
                    InflightPersistence &inflight_persistence,
                    OfflineQueuePersistence &offline_queue_persistence,
                    bool include_inflight_states,
                    SessionStore &session_store,
                    RetainedMessageStore &retained_store,
                    InflightStore &inflight_store,
                    OfflineQueue &offline_queue) noexcept;
};

} // namespace mqtt
