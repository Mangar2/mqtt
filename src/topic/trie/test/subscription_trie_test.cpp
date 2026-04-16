#include <catch2/catch_test_macros.hpp>

#include "data_model/subscription/subscription.h"
#include "data_model/types/qos.h"
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

// ── insert
// ────────────────────────────────────────────────────────────────────

TEST_CASE("insert_single", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("sport/tennis"));
  REQUIRE(trie.size() == 1);
}

TEST_CASE("insert_two_clients_same_filter", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b"));
  trie.insert("B", make_sub("a/b"));
  REQUIRE(trie.size() == 2);
}

TEST_CASE("insert_same_client_replaces", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b", mqtt::QoS::AtMostOnce));
  trie.insert("A", make_sub("a/b", mqtt::QoS::AtLeastOnce));
  REQUIRE(trie.size() == 1);
}

TEST_CASE("insert_same_client_different_filters", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b"));
  trie.insert("A", make_sub("a/c"));
  REQUIRE(trie.size() == 2);
}

TEST_CASE("insert_wildcard_plus", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("sport/+/player"));
  REQUIRE(trie.size() == 1);
}

TEST_CASE("insert_wildcard_hash", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("sport/#"));
  REQUIRE(trie.size() == 1);
}

TEST_CASE("insert_hash_only", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("#"));
  REQUIRE(trie.size() == 1);
}

// ── remove
// ────────────────────────────────────────────────────────────────────

TEST_CASE("remove_existing", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b"));
  trie.remove("A", "a/b");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_nonexistent_filter", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.remove("A", "x/y");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_wrong_client", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b"));
  trie.remove("B", "a/b");
  REQUIRE(trie.size() == 1);
}

TEST_CASE("remove_one_of_two_clients", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b"));
  trie.insert("B", make_sub("a/b"));
  trie.remove("A", "a/b");
  REQUIRE(trie.size() == 1);
}

TEST_CASE("remove_prunes_nodes", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("deep/path/here"));
  trie.remove("A", "deep/path/here");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_wildcard_plus", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("sport/+"));
  trie.remove("A", "sport/+");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_wildcard_hash", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("sport/#"));
  trie.remove("A", "sport/#");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_empty_trie", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.remove("A", "a/b");
  REQUIRE(trie.size() == 0);
}

// ── remove_all
// ────────────────────────────────────────────────────────────────

TEST_CASE("remove_all_single_client", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b"));
  trie.insert("A", make_sub("c/d"));
  trie.remove_all("A");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_all_leaves_other_clients", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("a/b"));
  trie.insert("A", make_sub("c/d"));
  trie.insert("B", make_sub("e/f"));
  trie.remove_all("A");
  REQUIRE(trie.size() == 1);
}

TEST_CASE("remove_all_nonexistent_client", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.remove_all("unknown");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_all_empty_trie", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.remove_all("A");
  REQUIRE(trie.size() == 0);
}

TEST_CASE("remove_all_with_wildcards", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("#"));
  trie.insert("A", make_sub("sport/+"));
  trie.insert("B", make_sub("a/b"));
  trie.remove_all("A");
  REQUIRE(trie.size() == 1);
}

TEST_CASE("remove_all_prunes_nodes", "[subscription_trie]") {
  mqtt::SubscriptionTrie trie;
  trie.insert("A", make_sub("x/y/z"));
  trie.remove_all("A");
  REQUIRE(trie.size() == 0);
}
