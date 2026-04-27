#pragma once

/**
 * @file sync_client.h
 * @brief Blocking MQTT client facade (Step 23).
 */

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "client/client_error.h"
#include "client_api/client_config.h"
#include "client/publish_pipeline.h"
#include "client/session_state_keeper.h"
#include "client/subscription_manager.h"
#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"

namespace mqtt {

/**
 * @brief Integration callbacks used by SyncClient blocking operations.
 */
struct SyncClientCallbacks {
  std::function<ConnectionNegotiationResult(const ConnectPacket &, uint32_t)>
      connect_and_negotiate;

  std::function<void(const PublishPacket &)> send_publish;
  std::function<PubackPacket(uint16_t, uint32_t)> wait_puback;
  std::function<PubrecPacket(uint16_t, uint32_t)> wait_pubrec;
  std::function<void(const PubrelPacket &)> send_pubrel;
  std::function<PubcompPacket(uint16_t, uint32_t)> wait_pubcomp;

  std::function<void(const SubscribePacket &)> send_subscribe;
  std::function<SubackPacket(uint16_t, uint32_t)> wait_suback;

  std::function<void(const UnsubscribePacket &)> send_unsubscribe;
  std::function<UnsubackPacket(uint16_t, uint32_t)> wait_unsuback;

  std::function<void(const DisconnectPacket &)> send_disconnect;
};

/**
 * @brief Blocking public MQTT client interface.
 *
 * The facade wraps lower-level components and exposes synchronous methods that
 * block until completion or timeout.
 */
class SyncClient {
public:
  /**
   * @brief Construct synchronous client facade.
   * @param client_id Logical client identifier.
   * @param reconnect_backoff Backoff settings for internal reconnect controller.
   */
  explicit SyncClient(
      std::string client_id,
      ReconnectBackoffPolicy reconnect_backoff = ReconnectBackoffPolicy{});

  /**
   * @brief Construct synchronous client facade from unified configuration.
   * @param client_config Public client configuration.
   */
  explicit SyncClient(ClientConfig client_config);

  /**
   * @brief Install integration callbacks.
   * @param callbacks Callback bundle used by blocking operations.
   */
  void set_callbacks(SyncClientCallbacks callbacks) noexcept;

    /**
     * @brief Perform blocking CONNECT using configured packet and timeout.
     * @return Negotiated connection result from CONNACK.
     */
    [[nodiscard]] ConnectionNegotiationResult connect();

    /**
     * @brief Perform blocking CONNECT negotiation with configured timeout.
     * @param connect_packet CONNECT packet model.
     * @return Negotiated connection result from CONNACK.
     */
    [[nodiscard]] ConnectionNegotiationResult
    connect(const ConnectPacket &connect_packet);

  /**
   * @brief Perform blocking CONNECT negotiation.
   * @param connect_packet CONNECT packet model.
   * @param timeout_ms Operation timeout in milliseconds.
   * @return Negotiated connection result from CONNACK.
   * @throws ClientException when callback is missing or negotiation fails.
   */
  [[nodiscard]] ConnectionNegotiationResult connect(
      const ConnectPacket &connect_packet, uint32_t timeout_ms);

    /**
     * @brief Perform blocking PUBLISH completion using configured timeout.
     * @param message Outbound message model.
     * @return Final MQTT reason code for the publish exchange.
     */
    [[nodiscard]] ReasonCode publish(const Message &message);

  /**
   * @brief Perform blocking PUBLISH completion according to QoS.
   * @param message Outbound message model.
   * @param timeout_ms Operation timeout in milliseconds.
   * @return Final MQTT reason code for the publish exchange.
   * @throws ClientException on connection/callback/protocol errors.
   */
  [[nodiscard]] ReasonCode publish(const Message &message,
                     uint32_t timeout_ms);

    /**
     * @brief Perform blocking SUBSCRIBE operation using configured timeout.
     * @param requests Subscription requests with callbacks.
     * @return Reason codes from SUBACK in request order.
     */
    [[nodiscard]] std::vector<ReasonCode> subscribe(
      const std::vector<ClientSubscriptionManager::SubscribeRequest> &requests);

  /**
   * @brief Perform blocking SUBSCRIBE operation.
   * @param requests Subscription requests with callbacks.
   * @param timeout_ms Operation timeout in milliseconds.
   * @return Reason codes from SUBACK in request order.
   * @throws ClientException on connection/callback/protocol errors.
   */
  [[nodiscard]] std::vector<ReasonCode> subscribe(
      const std::vector<ClientSubscriptionManager::SubscribeRequest> &requests,
      uint32_t timeout_ms);

    /**
     * @brief Perform blocking UNSUBSCRIBE operation using configured timeout.
     * @param topic_filters Filters to unsubscribe.
     * @return Reason codes from UNSUBACK in request order.
     */
    [[nodiscard]] std::vector<ReasonCode>
    unsubscribe(const std::vector<std::string> &topic_filters);

  /**
   * @brief Perform blocking UNSUBSCRIBE operation.
   * @param topic_filters Filters to unsubscribe.
   * @param timeout_ms Operation timeout in milliseconds.
   * @return Reason codes from UNSUBACK in request order.
   * @throws ClientException on connection/callback/protocol errors.
   */
  [[nodiscard]] std::vector<ReasonCode>
  unsubscribe(const std::vector<std::string> &topic_filters,
              uint32_t timeout_ms);

  /**
   * @brief Perform blocking DISCONNECT operation.
   * @param reason_code Disconnect reason code.
   * @throws ClientException when callback transport send fails.
   */
  void disconnect(ReasonCode reason_code = ReasonCode::Success);

  /**
   * @brief Return whether client is currently connected.
   */
  [[nodiscard]] bool is_connected() const noexcept;

  /**
   * @brief Return whether topic filter is currently active.
   * @param topic_filter Topic filter key.
   */
  [[nodiscard]] bool has_subscription(std::string_view topic_filter) const noexcept;

  /**
   * @brief Return current immutable client configuration.
   */
  [[nodiscard]] const ClientConfig &client_config() const noexcept;

  /**
   * @brief Dispatch one inbound publish packet to active subscription callbacks.
   * @param publish_packet Inbound packet from transport integration.
   * @return Number of invoked subscription callbacks.
   */
  [[nodiscard]] std::size_t
  dispatch_inbound_publish(const PublishPacket &publish_packet) const;

private:
  void require_connected() const;
  static void require_callback(bool has_callback, std::string_view operation,
                               ClientError error_code);
  [[nodiscard]] static std::optional<QoS>
  decode_granted_qos(ReasonCode reason_code) noexcept;

  ClientConfig client_config_{};
  std::string client_id_;
  bool connected_{false};
  SyncClientCallbacks callbacks_{};

  ClientSubscriptionManager subscription_manager_;
  ClientPublishPipeline publish_pipeline_;
  ClientSessionStateKeeper session_state_keeper_;
  ReconnectController reconnect_controller_;
};

} // namespace mqtt
