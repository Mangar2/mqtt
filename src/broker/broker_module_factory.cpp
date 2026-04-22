#include "broker/broker_module_factory.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "authz/broker_acl_policy.h"
#include "codec/packet/publish_codec.h"
#include "data_model/message/message.h"
#include "message_router/message_router_error.h"
#include "will_manager/will_message_util.h"

namespace mqtt {

namespace {

[[nodiscard]] std::size_t estimated_publish_frame_bytes(const Message &message) {
  PublishPacket packet;
  packet.dup = false;
  packet.qos = message.qos;
  packet.retain = message.retain;
  packet.topic = message.topic;
  if (message.qos != QoS::AtMostOnce) {
    packet.packet_id = 1U;
  }
  packet.payload = message.payload;
  packet.properties = message.properties;

  WriteBuffer encoded_frame;
  encode_publish(encoded_frame, packet);
  return encoded_frame.size();
}

} // namespace

void BrokerModuleFactory::create(
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
        client_handler_callback) {
  session_persistence = std::make_unique<SessionPersistence>(config.persistence_dir);
  retained_persistence =
      std::make_unique<RetainedMessagePersistence>(config.persistence_dir);
  inflight_persistence =
      std::make_unique<InflightPersistence>(config.persistence_dir);
  offline_queue_persistence =
      std::make_unique<OfflineQueuePersistence>(config.persistence_dir);

  session_store = std::make_unique<SessionStore>();
  retained_store = std::make_unique<RetainedMessageStore>();
  subscription_store = std::make_unique<SubscriptionStore>();
  inflight_store = std::make_unique<InflightStore>();

  if (config.allow_anonymous) {
    anonymous_authenticator =
        std::make_unique<AnonymousAuthenticator>(AnonymousPolicy::Allow);
    active_authenticator = anonymous_authenticator.get();
  } else {
    password_authenticator = std::make_unique<PasswordAuthenticator>();
    for (const PasswordCredentialConfig &credential : config.password_credentials) {
      password_authenticator->add_credential(
          Utf8String{credential.username},
          BinaryData::from_string(credential.password));
    }
    active_authenticator = password_authenticator.get();
  }

  acl_engine = std::make_unique<AclEngine>();
  acl_loader = std::make_unique<AclLoader>(*acl_engine);

  std::vector<AclRuleConfig> acl_rules =
      make_startup_acl_rules(config.acl_rules, config.allow_anonymous);
  acl_loader->load(acl_rules);

  takeover_handler = std::make_unique<SessionTakeoverHandler>();
  expiry_scheduler = std::make_unique<SessionExpiryScheduler>();
  session_manager = std::make_unique<SessionManager>(
      *session_store, *subscription_store, *inflight_store, *takeover_handler,
      *expiry_scheduler);

  publish_processor = std::make_unique<InboundPublishProcessor>(
      *acl_engine, *retained_store, *subscription_store);

  offline_queue =
      std::make_unique<OfflineQueue>(static_cast<std::size_t>(config.max_queued_messages));
  connection_registry = std::make_unique<ActiveConnectionRegistry>();
  shared_dispatcher = std::make_unique<SharedSubscriptionDispatcher>();

  auto is_online_function = [registry_ptr = connection_registry.get()](
                                std::string_view client_id) -> bool {
    return registry_ptr->contains(client_id);
  };
  auto deliver_function =
      [registry_ptr = connection_registry.get(), stats_ptr = &statistics_collector,
       tracer_ptr = &structured_tracer, router_config = config](
          std::string_view client_id, const Message &message) {
        std::shared_ptr<OutboundQueue> queue = registry_ptr->find(client_id);
        if (!queue) {
          return;
        }

        const std::size_t frame_bytes = estimated_publish_frame_bytes(message);
        if (frame_bytes > router_config.write_queue_max_bytes) {
          if (*tracer_ptr) {
            TRACE_GUARD(tracer_ptr->get(), TraceLevel::Warning, "broker") {
              TraceEvent event;
              event.level = TraceLevel::Warning;
              event.module = "broker";
              event.info = "outbound_frame_exceeds_write_queue_capacity";
              event.data.emplace_back("client_id", std::string(client_id));
              event.data.emplace_back("topic", message.topic.value);
              event.data.emplace_back("qos",
                                      std::to_string(static_cast<int>(message.qos)));
              event.data.emplace_back("payload_bytes",
                                      std::to_string(message.payload.data.size()));
              event.data.emplace_back("frame_bytes", std::to_string(frame_bytes));
              event.data.emplace_back(
                  "write_queue_max_bytes",
                  std::to_string(router_config.write_queue_max_bytes));
              tracer_ptr->get()->emit(event);
            }
          }
          throw MessageRouterException(
              MessageRouterError::QueueFull,
              "outbound frame exceeds write queue capacity");
        }

        if (*stats_ptr) {
          (*stats_ptr)->on_message_outbound();
        }
        const bool pushed = queue->push(message);
        if (!pushed) {
          throw MessageRouterException(MessageRouterError::QueueFull,
                                       "online outbound queue capacity exceeded");
        }
      };

    structured_tracer = std::make_unique<StructuredTracer>(std::clog);
    structured_tracer->set_global_level(config.trace_global_level);
    structured_tracer->set_trace_modules(config.trace_modules);
    structured_tracer->set_max_text_length(config.trace_max_text_length);

    message_router = std::make_unique<MessageRouter>(
      *publish_processor, *offline_queue, *shared_dispatcher,
      std::move(is_online_function), std::move(deliver_function),
      structured_tracer.get());

  subscription_orchestrator = std::make_unique<SubscriptionOrchestrator>(
      *acl_engine, *session_store, *subscription_store, *shared_dispatcher,
      *message_router);

  will_store = std::make_unique<WillStore>();
  will_delay_timer = std::make_unique<WillDelayTimer>();

  auto will_publish_function = [router_ptr = message_router.get()](
                                   const WillMessage &will_message) {
    Message message = will_message.message;
    router_ptr->route_internal(message, k_broker_internal_principal);
  };

  will_publisher = std::make_unique<WillPublisher>(
      *will_store, *will_delay_timer, std::move(will_publish_function));

  statistics_collector =
      std::make_unique<StatisticsCollector>(*subscription_store, *retained_store);

  session_manager->set_tracer(structured_tracer.get());

  auto sys_publish_function = [router_ptr = message_router.get()](Message message) {
    router_ptr->route_internal(std::move(message), k_broker_internal_principal);
  };
  sys_publisher = std::make_unique<SysTopicPublisher>(
      *statistics_collector, std::chrono::seconds(config.sys_topic_interval),
      std::move(sys_publish_function));

  connection_manager = std::make_unique<ConnectionManager>(
      config.mqtt_port, config.ws_port, std::move(client_handler_callback),
      std::chrono::seconds(2), 2U,
      static_cast<std::size_t>(std::max(2U, config.max_connections)),
      structured_tracer.get());
}

} // namespace mqtt
