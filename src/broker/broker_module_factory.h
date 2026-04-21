#pragma once

/**
 * @file broker_module_factory.h
 * @brief Module construction/wiring helper extracted from Broker.
 */

#include <functional>
#include <memory>

#include "auth/anonymous_authenticator.h"
#include "auth/authenticator.h"
#include "auth/password_authenticator.h"
#include "authz/acl_engine.h"
#include "authz/acl_loader.h"
#include "broker/active_connection_registry.h"
#include "broker/broker_config.h"
#include "connection/connection_manager.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/message_router.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/structured_tracer.h"
#include "monitoring/sys_topic_publisher.h"
#include "persistence/inflight_persistence.h"
#include "persistence/offline_queue_persistence.h"
#include "persistence/retained_message_persistence.h"
#include "persistence/session_persistence.h"
#include "session_manager/session_expiry_scheduler.h"
#include "session_manager/session_manager.h"
#include "session_manager/session_takeover_handler.h"
#include "store/inflight_store.h"
#include "store/retained_message_store.h"
#include "store/session_store.h"
#include "store/subscription_store.h"
#include "subscription_manager/subscription_orchestrator.h"
#include "will_manager/will_delay_timer.h"
#include "will_manager/will_publisher.h"
#include "will_manager/will_store.h"

namespace mqtt {

/**
 * @brief Factory that creates and wires broker module instances.
 */
class BrokerModuleFactory {
public:
  /**
   * @brief Create and wire all broker modules.
   */
  static void create(
      const BrokerConfig &config,
      std::unique_ptr<SessionPersistence> &session_persistence,
      std::unique_ptr<RetainedMessagePersistence> &retained_persistence,
      std::unique_ptr<InflightPersistence> &inflight_persistence,
      std::unique_ptr<OfflineQueuePersistence> &offline_queue_persistence,
      std::unique_ptr<SessionStore> &session_store,
      std::unique_ptr<RetainedMessageStore> &retained_store,
      std::unique_ptr<SubscriptionStore> &subscription_store,
      std::unique_ptr<InflightStore> &inflight_store,
      std::unique_ptr<AnonymousAuthenticator> &anonymous_authenticator,
      std::unique_ptr<PasswordAuthenticator> &password_authenticator,
      IAuthenticator *&active_authenticator, std::unique_ptr<AclEngine> &acl_engine,
      std::unique_ptr<AclLoader> &acl_loader,
      std::unique_ptr<SessionTakeoverHandler> &takeover_handler,
      std::unique_ptr<SessionExpiryScheduler> &expiry_scheduler,
      std::unique_ptr<SessionManager> &session_manager,
      std::unique_ptr<InboundPublishProcessor> &publish_processor,
      std::unique_ptr<OfflineQueue> &offline_queue,
      std::unique_ptr<SharedSubscriptionDispatcher> &shared_dispatcher,
      std::unique_ptr<SubscriptionOrchestrator> &subscription_orchestrator,
      std::unique_ptr<MessageRouter> &message_router,
      std::unique_ptr<ActiveConnectionRegistry> &connection_registry,
      std::unique_ptr<WillStore> &will_store,
      std::unique_ptr<WillDelayTimer> &will_delay_timer,
      std::unique_ptr<WillPublisher> &will_publisher,
      std::unique_ptr<StatisticsCollector> &statistics_collector,
      std::unique_ptr<StructuredTracer> &structured_tracer,
      std::unique_ptr<SysTopicPublisher> &sys_publisher,
      std::unique_ptr<ConnectionManager> &connection_manager,
      std::function<void(std::unique_ptr<TcpConnection>, bool)>
          client_handler_callback);
};

} // namespace mqtt
