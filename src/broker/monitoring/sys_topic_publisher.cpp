/**
 * @file sys_topic_publisher.cpp
 * @brief SysTopicPublisher implementation (Module 16.2).
 */

#include "monitoring/sys_topic_publisher.h"

#include <format>
#include <string>
#include <vector>

#include "data_model/types/binary_data.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

SysTopicPublisher::SysTopicPublisher(const StatisticsCollector &stats,
                                     std::chrono::seconds interval,
                                     PublishFn publish_fn)
    : stats_(stats), interval_(interval), publish_fn_(std::move(publish_fn)) {}

bool SysTopicPublisher::tick(std::chrono::steady_clock::time_point now) {
  if (interval_.count() <= 0) {
    return false;
  }

  if ((now - last_publish_) < interval_) {
    return false;
  }

  last_publish_ = now;
  publish_stats(stats_.snapshot());
  return true;
}

void SysTopicPublisher::publish_stats(const StatisticsSnapshot &snap) {
  publish_one("$SYS/broker/clients/connected",
              static_cast<std::uint64_t>(snap.connected_clients));
  publish_one("$SYS/broker/messages/received", snap.messages_inbound);
  publish_one("$SYS/broker/messages/sent", snap.messages_outbound);
  publish_one("$SYS/broker/subscriptions/count",
              static_cast<std::uint64_t>(snap.active_subscriptions));
  publish_one("$SYS/broker/retained messages/count",
              static_cast<std::uint64_t>(snap.retained_messages));
  publish_one("$SYS/broker/uptime",
              static_cast<std::uint64_t>(snap.uptime.count()));
}

void SysTopicPublisher::publish_one(std::string_view topic_name,
                                    std::uint64_t value) {
  const std::string text = std::format("{}", value);
  Message msg;
  msg.topic = Utf8String{std::string(topic_name)};
  msg.payload = BinaryData{std::vector<std::uint8_t>(text.begin(), text.end())};
  msg.qos = QoS::AtMostOnce;
  msg.retain = true;
  publish_fn_(std::move(msg));
}

} // namespace mqtt
