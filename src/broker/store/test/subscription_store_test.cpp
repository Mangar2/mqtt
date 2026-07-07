#include <catch2/catch_test_macros.hpp>

#include "data_model/subscription/subscription.h"
#include "data_model/types/qos.h"
#include "store/subscription_store.h"

using namespace mqtt;

namespace {

Subscription make_sub(const std::string &filter, QoS qos = QoS::AtMostOnce) {
  Subscription sub;
  sub.topic_filter.value = filter;
  sub.qos = qos;
  return sub;
}

} // namespace

TEST_CASE("store_insert_and_size", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b"));
  CHECK(store.size() == 1U);
}

TEST_CASE("store_overwrite_same_filter", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b", QoS::AtMostOnce));
  store.store("c1", make_sub("a/b", QoS::AtLeastOnce));
  CHECK(store.size() == 1U);

  const auto results = store.subscribers_for("a/b");
  REQUIRE(results.size() == 1U);
  CHECK(results.front().subscription.qos == QoS::AtLeastOnce);
}

TEST_CASE("store_two_clients_same_filter", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b"));
  store.store("c2", make_sub("a/b"));
  CHECK(store.size() == 2U);
}

TEST_CASE("remove_existing_subscription", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b"));
  store.remove("c1", "a/b");
  CHECK(store.size() == 0U);
}

TEST_CASE("remove_nonexistent_is_noop", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b"));
  store.remove("c1", "x/y");
  CHECK(store.size() == 1U);
}

TEST_CASE("subscribers_for_exact_match", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b"));
  const auto results = store.subscribers_for("a/b");
  REQUIRE(results.size() == 1U);
  CHECK(results.front().client_id == "c1");
}

TEST_CASE("subscribers_for_wildcard_plus", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/+/c"));
  const auto results = store.subscribers_for("a/b/c");
  CHECK(results.size() == 1U);
}

TEST_CASE("subscribers_for_wildcard_hash", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/#"));
  const auto results = store.subscribers_for("a/b/c/d");
  CHECK(results.size() == 1U);
}

TEST_CASE("subscribers_for_no_match", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("x/y"));
  const auto results = store.subscribers_for("a/b");
  CHECK(results.empty());
}

TEST_CASE("remove_session_clears_all", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b"));
  store.store("c1", make_sub("x/y"));
  store.remove_session("c1");
  CHECK(store.size() == 0U);
}

TEST_CASE("remove_session_noop_unknown", "[store]") {
  SubscriptionStore store;
  store.store("c1", make_sub("a/b"));
  store.remove_session("unknown-client");
  CHECK(store.size() == 1U);
}
