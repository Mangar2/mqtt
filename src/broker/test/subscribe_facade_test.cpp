#include <catch2/catch_test_macros.hpp>

#include "broker/subscribe_facade.h"

#include "authz/acl_rule.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/reason_code/reason_code.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/message_router.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "monitoring/structured_tracer.h"
#include "store/retained_message_store.h"
#include "store/session_store.h"
#include "store/subscription_store.h"
#include "subscription_manager/subscription_orchestrator.h"

#include <sstream>
#include <string>
#include <vector>

namespace mqtt {
namespace {

struct SubscribeFacadeFixture {
  std::ostringstream trace_stream{};
  StructuredTracer tracer{trace_stream};
  AclEngine acl_engine{};
  SessionStore session_store{};
  SubscriptionStore subscription_store{};
  RetainedMessageStore retained_store{};
  OfflineQueue offline_queue{};
  SharedSubscriptionDispatcher shared_dispatcher{};
  InboundPublishProcessor inbound_processor{acl_engine, retained_store,
                                            subscription_store};
  MessageRouter message_router{inbound_processor,
                               offline_queue,
                               shared_dispatcher,
                               [](std::string_view) { return false; },
                               [](std::string_view, const Message &) {},
                               &tracer};
  SubscriptionOrchestrator orchestrator{acl_engine,
                                        session_store,
                                        subscription_store,
                                        shared_dispatcher,
                                        message_router};
  SubscribeFacade facade{orchestrator, tracer};

  SubscribeFacadeFixture() {
    tracer.set_global_level(TraceLevel::Info);
  }

  void allow_all_subscribe() {
    acl_engine.reload({AclRule{.principal = "*",
                               .topic_pattern = "#",
                               .action = AclAction::Subscribe,
                               .effect = AclEffect::Allow}});
  }
};

constexpr std::uint16_t k_subscribe_packet_identifier{17U};
constexpr std::uint16_t k_unsubscribe_packet_identifier{23U};
constexpr std::size_t k_subscribe_filter_count{11U};
constexpr std::size_t k_unsubscribe_valid_filter_count{10U};

SubscribePacket make_subscribe_packet_with_many_filters() {
  SubscribePacket packet{};
  packet.packet_id = k_subscribe_packet_identifier;

  for (std::size_t index_value = 0U; index_value < k_subscribe_filter_count; ++index_value) {
    packet.filters.push_back(
        SubscribeFilter{.topic_filter = Utf8String{"trace/" + std::to_string(index_value)},
                        .options = SubscribeOptions{.max_qos = QoS::AtMostOnce,
                                                    .no_local = false,
                                                    .retain_as_published = false,
                                                    .retain_handling = 0U}});
  }

  return packet;
}

UnsubscribePacket make_unsubscribe_packet_with_many_filters_and_one_invalid() {
  UnsubscribePacket packet{};
  packet.packet_id = k_unsubscribe_packet_identifier;

  for (std::size_t index_value = 0U;
       index_value < k_unsubscribe_valid_filter_count;
       ++index_value) {
    packet.topic_filters.push_back(
        Utf8String{"trace/unsub/" + std::to_string(index_value)});
  }
  packet.topic_filters.push_back(Utf8String{"trace/#/invalid"});
  return packet;
}

} // namespace

TEST_CASE("subscribe_facade_trace_limits_topic_filter_list", "[broker]") {
  SubscribeFacadeFixture fixture{};
  fixture.allow_all_subscribe();

  const SubscribePacket packet = make_subscribe_packet_with_many_filters();
  const SubackPacket suback = fixture.facade.handle_subscribe("trace_client", packet);

  REQUIRE(suback.reason_codes.size() == packet.filters.size());
  REQUIRE(suback.reason_codes.front() == ReasonCode::Success);
}

TEST_CASE("subscribe_facade_unsubscribe_trace_reports_failed_filters", "[broker]") {
  SubscribeFacadeFixture fixture{};
  fixture.allow_all_subscribe();

  const UnsubscribePacket packet = make_unsubscribe_packet_with_many_filters_and_one_invalid();
  const UnsubackPacket unsuback = fixture.facade.handle_unsubscribe("trace_client", packet);

  REQUIRE(unsuback.reason_codes.size() == packet.topic_filters.size());
  CHECK(unsuback.reason_codes.back() == ReasonCode::TopicFilterInvalid);
}

} // namespace mqtt
