#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "client_api/client_api_error.h"
#include "client_api/sync_client.h"

namespace mqtt {

namespace {

[[nodiscard]] ConnectPacket make_connect_packet() {
  ConnectPacket connect_packet;
  connect_packet.client_id = Utf8String{"error-client"};
  connect_packet.clean_start = true;
  connect_packet.keep_alive = 30U;
  return connect_packet;
}

[[nodiscard]] Message make_qos1_message() {
  Message message;
  message.topic = Utf8String{"topic/error"};
  message.qos = QoS::AtLeastOnce;
  message.payload = BinaryData{{0x45U}};
  return message;
}

[[nodiscard]] ConnectionNegotiationResult make_connect_result() {
  ConnectionNegotiationResult connect_result;
  connect_result.session_present = false;
  connect_result.reason_code = ReasonCode::Success;
  return connect_result;
}

} // namespace

TEST_CASE("client_api_error_from_client_exception_maps_network_timeout_and_protocol",
          "[client_api][error]") {
  const ClientApiError network_error =
      client_api_error_from_client_exception(
          ClientException(ClientError::ConnectFailed, "dial failed"));
  CHECK(network_error.category == ClientApiErrorCategory::Network);
  CHECK(network_error.message == "dial failed");

  const ClientApiError timeout_error =
      client_api_error_from_client_exception(
          ClientException(ClientError::Timeout, "wait timed out"));
  CHECK(timeout_error.category == ClientApiErrorCategory::Timeout);

  const ClientApiError protocol_error =
      client_api_error_from_client_exception(
          ClientException(ClientError::ProtocolError, "bad packet"));
  CHECK(protocol_error.category == ClientApiErrorCategory::Protocol);
}

TEST_CASE(
    "client_api_error_reason_code_classification_covers_auth_and_authorization",
    "[client_api][error]") {
  CHECK(classify_reason_code_category(ReasonCode::BadUserNameOrPassword) ==
        ClientApiErrorCategory::Authentication);
  CHECK(classify_reason_code_category(ReasonCode::BadAuthenticationMethod) ==
        ClientApiErrorCategory::Authentication);
  CHECK(classify_reason_code_category(ReasonCode::NotAuthorized) ==
        ClientApiErrorCategory::Authorization);

  const ClientApiError broker_error = client_api_error_from_reason_code(
      ReasonCode::ServerBusy, "server busy");
  CHECK(broker_error.category == ClientApiErrorCategory::Broker);
  CHECK(broker_error.reason_code == ReasonCode::ServerBusy);
}

TEST_CASE("client_api_error_from_std_exception_is_unknown_category",
          "[client_api][error]") {
  const std::runtime_error runtime_error("unexpected std failure");
  const ClientApiError api_error = client_api_error_from_std_exception(runtime_error);
  CHECK(api_error.category == ClientApiErrorCategory::Unknown);
  CHECK(api_error.message == "unexpected std failure");
  CHECK_FALSE(api_error.reason_code.has_value());
}

  TEST_CASE(
    "client_api_error_mapping_covers_configuration_and_reason_override_paths",
    "[client_api][error]") {
    const ClientApiError configuration_error =
      client_api_error_from_client_exception(
        ClientException(ClientError::InvalidPacket, "invalid config field"));
    CHECK(configuration_error.category == ClientApiErrorCategory::Configuration);

    const ClientApiError broker_from_internal_error =
      client_api_error_from_client_exception(ClientException(
        ClientError::NegotiationRejected, "negotiation rejected"));
    CHECK(broker_from_internal_error.category == ClientApiErrorCategory::Broker);

    const ClientApiError reason_override_error =
      client_api_error_from_client_exception(ClientException(
        ClientError::ProtocolError, "not authorized reason overrides source",
        ReasonCode::NotAuthorized));
    CHECK(reason_override_error.category == ClientApiErrorCategory::Authorization);

    CHECK(classify_reason_code_category(ReasonCode::ProtocolError) ==
      ClientApiErrorCategory::Protocol);
    CHECK(classify_reason_code_category(ReasonCode::KeepAliveTimeout) ==
      ClientApiErrorCategory::Timeout);
    CHECK(classify_reason_code_category(ReasonCode::Success) ==
      ClientApiErrorCategory::Unknown);
  }

TEST_CASE("sync_client_throws_client_api_exception_for_broker_reason_code_errors",
          "[client_api][error]") {
  SyncClient client("error-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_publish = [](const PublishPacket &) {};
  callbacks.wait_puback = [](uint16_t packet_id, uint32_t) {
    return PubackPacket{.packet_id = packet_id,
                        .reason_code = ReasonCode::NotAuthorized,
                        .properties = {}};
  };
  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  try {
    (void)client.publish(make_qos1_message(), 1000U);
    FAIL("expected ClientApiException");
  } catch (const ClientApiException &exception) {
    CHECK(exception.error().category == ClientApiErrorCategory::Authorization);
    REQUIRE(exception.error().reason_code.has_value());
    CHECK(*exception.error().reason_code == ReasonCode::NotAuthorized);
  }
}

} // namespace mqtt
