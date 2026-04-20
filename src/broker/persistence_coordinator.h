#pragma once

/**
 * @file persistence_coordinator.h
 * @brief Persistence load/flush helper extracted from Broker.
 */

#include "persistence/inflight_persistence.h"
#include "persistence/retained_message_persistence.h"
#include "persistence/session_persistence.h"
#include "store/inflight_store.h"
#include "store/retained_message_store.h"
#include "store/session_store.h"
#include "store/subscription_store.h"

namespace mqtt {

/**
 * @brief Coordinates persistence load/flush over store and adapter objects.
 */
class PersistenceCoordinator {
public:
  /**
   * @brief Restore persisted sessions, retained messages, and inflight entries.
   */
  static void load(SessionPersistence &session_persistence,
                   RetainedMessagePersistence &retained_persistence,
                   InflightPersistence &inflight_persistence,
                   SessionStore &session_store,
                   RetainedMessageStore &retained_store,
                   SubscriptionStore &subscription_store,
                   InflightStore &inflight_store);

  /**
   * @brief Persist current sessions, retained messages, and inflight entries.
   *
   * This method is noexcept by design for shutdown paths.
   */
  static void flush(SessionPersistence &session_persistence,
                    RetainedMessagePersistence &retained_persistence,
                    InflightPersistence &inflight_persistence,
                    SessionStore &session_store,
                    RetainedMessageStore &retained_store,
                    InflightStore &inflight_store) noexcept;
};

} // namespace mqtt
