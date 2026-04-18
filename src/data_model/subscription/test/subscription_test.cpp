#include <catch2/catch_test_macros.hpp>

#include "data_model/subscription/retain_handling.h"
#include "data_model/subscription/shared_subscription.h"
#include "data_model/subscription/subscription.h"
#include "data_model/subscription/subscription_options.h"

using namespace mqtt;

//  RetainHandling (1.6.2)
//

TEST_CASE("retain_handling_values", "[subscription]") {
  CHECK(static_cast<uint8_t>(RetainHandling::SendAtSubscribe) == 0U);
  CHECK(static_cast<uint8_t>(RetainHandling::SendIfNew) == 1U);
  CHECK(static_cast<uint8_t>(RetainHandling::Never) == 2U);
}

//  SubscriptionOptions (1.6.2)
//

TEST_CASE("subscription_options_defaults", "[subscription]") {
  SubscriptionOptions opts{};
  CHECK(opts.no_local == false);
  CHECK(opts.retain_as_published == false);
  CHECK(opts.retain_handling == RetainHandling::SendAtSubscribe);
}

TEST_CASE("subscription_options_set_fields", "[subscription]") {
  SubscriptionOptions opts{};
  opts.no_local = true;
  opts.retain_as_published = true;
  opts.retain_handling = RetainHandling::Never;

  CHECK(opts.no_local == true);
  CHECK(opts.retain_as_published == true);
  CHECK(opts.retain_handling == RetainHandling::Never);
}

TEST_CASE("subscription_options_equality", "[subscription]") {
  SubscriptionOptions lhs{};
  SubscriptionOptions rhs{};
  CHECK(lhs == rhs);
}

TEST_CASE("subscription_options_inequality", "[subscription]") {
  SubscriptionOptions lhs{};
  SubscriptionOptions rhs{};
  rhs.no_local = true;
  CHECK(lhs != rhs);
}

//  Subscription (1.6.1)
//

TEST_CASE("subscription_defaults", "[subscription]") {
  Subscription sub{};
  CHECK(sub.topic_filter.value.empty());
  CHECK(sub.qos == QoS::AtMostOnce);
  CHECK(sub.options == SubscriptionOptions{});
  CHECK(!sub.identifier.has_value());
}

TEST_CASE("subscription_set_fields", "[subscription]") {
  Subscription sub{};
  sub.topic_filter.value = "sensors/+";
  sub.qos = QoS::AtLeastOnce;
  sub.options.no_local = true;
  sub.identifier = 42U;

  CHECK(sub.topic_filter.value == "sensors/+");
  CHECK(sub.qos == QoS::AtLeastOnce);
  CHECK(sub.options.no_local == true);
  CHECK(sub.identifier == 42U);
}

TEST_CASE("subscription_equality", "[subscription]") {
  Subscription lhs{};
  Subscription rhs{};
  CHECK(lhs == rhs);
}

TEST_CASE("subscription_inequality", "[subscription]") {
  Subscription lhs{};
  Subscription rhs{};
  rhs.qos = QoS::ExactlyOnce;
  CHECK(lhs != rhs);
}

TEST_CASE("subscription_with_identifier", "[subscription]") {
  Subscription sub{};
  sub.identifier = 42U;
  CHECK(sub.identifier.has_value());
  CHECK(sub.identifier.value() == 42U);
}

TEST_CASE("subscription_without_identifier", "[subscription]") {
  Subscription sub{};
  CHECK(!sub.identifier.has_value());
}

//  SharedSubscription (1.6.3)
//

TEST_CASE("shared_subscription_defaults", "[subscription]") {
  SharedSubscription subs{};
  CHECK(subs.group.value.empty());
  CHECK(subs.topic_filter.value.empty());
}

TEST_CASE("shared_subscription_set_fields", "[subscription]") {
  SharedSubscription subs{};
  subs.group.value = "g1";
  subs.topic_filter.value = "sensors/+";

  CHECK(subs.group.value == "g1");
  CHECK(subs.topic_filter.value == "sensors/+");
}

TEST_CASE("shared_subscription_equality", "[subscription]") {
  SharedSubscription lhs{};
  SharedSubscription rhs{};
  CHECK(lhs == rhs);
}

TEST_CASE("shared_subscription_inequality", "[subscription]") {
  SharedSubscription lhs{};
  SharedSubscription rhs{};
  rhs.group.value = "g2";
  CHECK(lhs != rhs);
}
