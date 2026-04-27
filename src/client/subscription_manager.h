#pragma once

/**
 * @file subscription_manager.h
 * @brief Client-side subscription manager for SUBSCRIBE/UNSUBSCRIBE workflows.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "client/client_error.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/subscription/subscription.h"
#include "topic/subscription_trie.h"

namespace mqtt {

/**
 * @brief Client-side orchestrator for SUBSCRIBE/UNSUBSCRIBE and inbound dispatch.
 *
 * Responsibilities:
 * - Build outbound SUBSCRIBE/UNSUBSCRIBE packets with packet IDs.
 * - Match SUBACK/UNSUBACK packets against pending requests.
 * - Maintain active subscription table with per-filter callbacks.
 * - Dispatch inbound PUBLISH packets to matching callbacks.
 */
class ClientSubscriptionManager {
public:
  /**
   * @brief Callback type invoked for inbound publishes matching one filter.
   */
  using MessageCallback = std::function<void(const PublishPacket &)>;

  /**
   * @brief One outbound subscribe request item.
   */
  struct SubscribeRequest {
    std::string topic_filter;
    QoS requested_qos{QoS::AtMostOnce};
    SubscriptionOptions options{};
    MessageCallback callback;
  };

  /**
   * @brief Result returned when beginning a subscribe operation.
   */
  struct SubscribeOperation {
    uint16_t packet_id{0};
    SubscribePacket packet;
  };

  /**
   * @brief Result returned when beginning an unsubscribe operation.
   */
  struct UnsubscribeOperation {
    uint16_t packet_id{0};
    UnsubscribePacket packet;
  };

  /**
   * @brief Generic ACK processing result.
   */
  struct AckResult {
    uint16_t packet_id{0};
    std::vector<ReasonCode> reason_codes;
  };

  /**
   * @brief Construct manager for one client identifier.
   * @param client_id Owning client identifier used for local trie entries.
   */
  explicit ClientSubscriptionManager(std::string client_id);

  /**
   * @brief Build a SUBSCRIBE packet and register pending state.
   * @param requests Requested topic filters with callbacks.
   * @return Packet ID and packet bytes model to send.
   * @throws ClientException when requests are empty or invalid.
   */
  [[nodiscard]] SubscribeOperation
  begin_subscribe(const std::vector<SubscribeRequest> &requests);

  /**
   * @brief Process a matching SUBACK and activate accepted subscriptions.
   * @param suback_packet Received SUBACK packet.
   * @return Processed packet id plus broker reason codes.
   * @throws ClientException on unknown packet id or malformed reason mapping.
   */
  [[nodiscard]] AckResult on_suback(const SubackPacket &suback_packet);

  /**
   * @brief Build an UNSUBSCRIBE packet and register pending state.
   * @param topic_filters Requested topic filters to remove.
   * @return Packet ID and packet model to send.
   * @throws ClientException when topic_filters are empty or invalid.
   */
  [[nodiscard]] UnsubscribeOperation
  begin_unsubscribe(const std::vector<std::string> &topic_filters);

  /**
   * @brief Process a matching UNSUBACK and remove successful filters.
   * @param unsuback_packet Received UNSUBACK packet.
   * @return Processed packet id plus broker reason codes.
   * @throws ClientException on unknown packet id or malformed reason mapping.
   */
  [[nodiscard]] AckResult on_unsuback(const UnsubackPacket &unsuback_packet);

  /**
   * @brief Dispatch inbound publish to all matching callbacks.
   * @param publish_packet Inbound publish packet from broker.
   * @return Number of invoked callbacks.
   * @throws ClientException when topic name is invalid.
   */
  [[nodiscard]] std::size_t
  dispatch_inbound_publish(const PublishPacket &publish_packet) const;

  /**
   * @brief Test whether a specific topic filter is active.
   * @param topic_filter Topic filter key.
   * @return True when active.
   */
  [[nodiscard]] bool has_subscription(std::string_view topic_filter) const noexcept;

  /**
   * @brief Number of active topic filters.
   */
  [[nodiscard]] std::size_t subscription_count() const noexcept;

  /**
   * @brief Remove all active and pending state.
   */
  void clear();

private:
  struct ActiveSubscription {
    Subscription subscription;
    MessageCallback callback;
  };

  struct PendingSubscribe {
    std::vector<SubscribeRequest> requests;
  };

  struct PendingUnsubscribe {
    std::vector<std::string> topic_filters;
  };

  [[nodiscard]] uint16_t allocate_packet_id();
  void release_packet_id(uint16_t packet_id) noexcept;
  [[nodiscard]] static SubscribeOptions
  to_wire_subscribe_options(const SubscriptionOptions &options) noexcept;
  [[nodiscard]] static std::optional<QoS>
  decode_granted_qos(ReasonCode reason_code) noexcept;
  static void validate_filter_or_throw(std::string_view topic_filter);
  static void validate_topic_name_or_throw(std::string_view topic_name);
  void activate_subscription(const SubscribeRequest &request, QoS granted_qos);
  void remove_subscription(std::string_view topic_filter);

  std::string client_id_;
  uint16_t last_packet_id_{0U};
  std::unordered_set<uint16_t> packet_ids_in_use_;
  std::unordered_map<uint16_t, PendingSubscribe> pending_subscribes_;
  std::unordered_map<uint16_t, PendingUnsubscribe> pending_unsubscribes_;
  std::unordered_map<std::string, ActiveSubscription> active_subscriptions_;
  SubscriptionTrie subscription_trie_;
};

} // namespace mqtt
