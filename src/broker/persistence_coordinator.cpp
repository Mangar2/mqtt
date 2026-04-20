#include "broker/persistence_coordinator.h"

#include <vector>

namespace mqtt {

void PersistenceCoordinator::load(SessionPersistence &session_persistence,
                                  RetainedMessagePersistence &retained_persistence,
                                  InflightPersistence &inflight_persistence,
                                  SessionStore &session_store,
                                  RetainedMessageStore &retained_store,
                                  SubscriptionStore &subscription_store,
                                  InflightStore &inflight_store) {
  const auto sessions = session_persistence.load_all();
  for (const auto &session_state : sessions) {
    session_store.create(session_state);
    for (const auto &subscription : session_state.subscriptions) {
      subscription_store.store(session_state.client_id.value, subscription);
    }
  }

  const auto retained_messages = retained_persistence.load_all();
  for (const auto &message : retained_messages) {
    retained_store.store(message);
  }

  const auto inflight_entries = inflight_persistence.load_all();
  for (const auto &entry : inflight_entries) {
    inflight_store.create(entry.client_id, entry.entry);
  }
}

void PersistenceCoordinator::flush(SessionPersistence &session_persistence,
                                   RetainedMessagePersistence &retained_persistence,
                                   InflightPersistence &inflight_persistence,
                                   SessionStore &session_store,
                                   RetainedMessageStore &retained_store,
                                   InflightStore &inflight_store) noexcept {
  try {
    std::vector<SessionState> sessions = session_store.all();
    session_persistence.save_all(sessions);

    std::vector<Message> retained_messages = retained_store.all();
    retained_persistence.save_all(retained_messages);

    std::vector<InflightPersistence::ClientEntry> inflight_entries;
    for (const auto &session_state : sessions) {
      const auto client_entries =
          inflight_store.entries_for(session_state.client_id.value);
      for (const auto &entry : client_entries) {
        inflight_entries.push_back(
            {.client_id = session_state.client_id.value, .entry = entry});
      }
    }
    inflight_persistence.save_all(inflight_entries);
  } catch (...) {
    // noexcept — swallow persistence errors during shutdown.
  }
}

} // namespace mqtt
