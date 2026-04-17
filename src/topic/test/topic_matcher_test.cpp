#include <catch2/catch_test_macros.hpp>

#include "data_model/subscription/subscription.h"
#include "data_model/types/qos.h"
#include "topic/topic_matcher.h"
#include "topic/trie/subscription_trie.h"

namespace {

mqtt::Subscription make_sub(const std::string &filter,
                            mqtt::QoS qos = mqtt::QoS::AtMostOnce) {
  mqtt::Subscription sub;
  sub.topic_filter.value = filter;
  sub.qos = qos;
  return sub;
}

} // namespace

// ── Exact matching (3.3.1)
// ──────────────────────────────────────────────────────

TEST_CASE("match_exact_match", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport/tennis"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  REQUIRE(results.size() == 1);
  CHECK(results[0].client_id == "client_a");
}

TEST_CASE("match_exact_no_match", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport/tennis"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/golf");
  CHECK(results.empty());
}

TEST_CASE("match_exact_prefix_no_match", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  CHECK(results.empty());
}

TEST_CASE("match_empty_trie", "[topic_matcher]") {
  const mqtt::SubscriptionTrie trie;
  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  CHECK(results.empty());
}

TEST_CASE("match_returns_correct_subscription", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport", mqtt::QoS::AtLeastOnce));

  const auto results = mqtt::TopicMatcher::match(trie, "sport");
  REQUIRE(results.size() == 1);
  CHECK(results[0].client_id == "client_a");
  CHECK(results[0].subscription.qos == mqtt::QoS::AtLeastOnce);
  CHECK(results[0].subscription.topic_filter.value == "sport");
}

// ── Single-level wildcard '+' (3.3.2)
// ──────────────────────────────────────────

TEST_CASE("match_plus_single_level", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport/+/player"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis/player");
  REQUIRE(results.size() == 1);
  CHECK(results[0].client_id == "client_a");
}

TEST_CASE("match_plus_multiple_in_filter", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("+/+/+"));

  const auto results = mqtt::TopicMatcher::match(trie, "a/b/c");
  REQUIRE(results.size() == 1);
}

TEST_CASE("match_plus_root_level", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("+"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport");
  REQUIRE(results.size() == 1);
}

TEST_CASE("match_plus_no_multi_level", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("+"));

  // '+' matches exactly one level — not a multi-level topic
  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  CHECK(results.empty());
}

// ── Multi-level wildcard '#' (3.3.3)
// ───────────────────────────────────────

TEST_CASE("match_hash_only", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("#"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  REQUIRE(results.size() == 1);
}

TEST_CASE("match_hash_multi_level", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport/#"));

  const auto results =
      mqtt::TopicMatcher::match(trie, "sport/tennis/wimbledon");
  REQUIRE(results.size() == 1);
}

TEST_CASE("match_hash_zero_remaining", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport/#"));

  // MQTT spec: 'sport/#' matches 'sport' (zero sub-levels)
  const auto results = mqtt::TopicMatcher::match(trie, "sport");
  REQUIRE(results.size() == 1);
}

TEST_CASE("match_hash_and_exact", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport/#"));
  trie.insert("client_b", make_sub("sport/tennis"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  CHECK(results.size() == 2);
}

// ── System topic exclusion (3.3.4)
// ─────────────────────────────────────────

TEST_CASE("match_system_topic_excluded_from_hash", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("#"));

  // '#' must not match topics beginning with '$'
  const auto results = mqtt::TopicMatcher::match(trie, "$SYS/info");
  CHECK(results.empty());
}

TEST_CASE("match_system_topic_excluded_from_plus", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("+/info"));

  // '+' at the first level must not match a '$'-prefixed topic
  const auto results = mqtt::TopicMatcher::match(trie, "$SYS/info");
  CHECK(results.empty());
}

TEST_CASE("match_system_topic_exact", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("$SYS/info"));

  const auto results = mqtt::TopicMatcher::match(trie, "$SYS/info");
  REQUIRE(results.size() == 1);
}

TEST_CASE("match_system_topic_hash_after_prefix", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("$SYS/#"));

  // Wildcard after exact '$SYS' level is allowed
  const auto results = mqtt::TopicMatcher::match(trie, "$SYS/info");
  REQUIRE(results.size() == 1);
}

TEST_CASE("match_system_topic_plus_after_prefix", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("$SYS/+"));

  const auto results = mqtt::TopicMatcher::match(trie, "$SYS/info");
  REQUIRE(results.size() == 1);
}

// ── Multi-client scenarios
// ─────────────────────────────────────────────────────

TEST_CASE("match_multiple_clients_same_filter", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("client_a", make_sub("sport/#"));
  trie.insert("client_b", make_sub("sport/#"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  CHECK(results.size() == 2);
}

TEST_CASE("match_multiple_overlapping_filters", "[topic_matcher]") {
  mqtt::SubscriptionTrie trie;
  // Same client has two overlapping subscriptions
  trie.insert("client_a", make_sub("#"));
  trie.insert("client_a", make_sub("sport/#"));

  const auto results = mqtt::TopicMatcher::match(trie, "sport/tennis");
  CHECK(results.size() == 2);
}
