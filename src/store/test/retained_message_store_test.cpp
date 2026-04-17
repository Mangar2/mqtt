#include <catch2/catch_test_macros.hpp>

#include "data_model/message/message.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"
#include "store/retained_message_store.h"

using namespace mqtt;

namespace {

Message make_message(const std::string &topic,
                     const std::string &payload = "data") {
  Message msg;
  msg.topic.value = topic;
  msg.payload.data = std::vector<uint8_t>(payload.begin(), payload.end());
  msg.retain = true;
  return msg;
}

} // namespace

TEST_CASE("store_creates_entry", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("a/b", "hello"));
  CHECK(store.size() == 1U);
}

TEST_CASE("store_overwrites_existing", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("a/b", "first"));
  store.store(make_message("a/b", "second"));
  CHECK(store.size() == 1U);

  const auto results = store.find("a/b");
  REQUIRE(results.size() == 1U);
  const std::string payload(results.front().payload.data.begin(),
                            results.front().payload.data.end());
  CHECK(payload == "second");
}

TEST_CASE("store_empty_payload_deletes", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("a/b", "hello"));
  store.store(make_message("a/b", ""));
  CHECK(store.size() == 0U);
}

TEST_CASE("store_empty_payload_noop_if_absent", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("x/y", ""));
  CHECK(store.size() == 0U);
}

TEST_CASE("find_exact_match", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("a/b"));
  const auto results = store.find("a/b");
  CHECK(results.size() == 1U);
}

TEST_CASE("find_plus_wildcard", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("a/b"));
  const auto results = store.find("a/+");
  CHECK(results.size() == 1U);
}

TEST_CASE("find_hash_wildcard", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("a/b"));
  store.store(make_message("a/b/c"));
  const auto results = store.find("a/#");
  CHECK(results.size() == 2U);
}

TEST_CASE("find_no_match", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("x/y"));
  const auto results = store.find("a/b");
  CHECK(results.empty());
}

TEST_CASE("find_system_topic_excluded_from_wildcard", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("$SYS/b"));
  const auto results = store.find("+/b");
  CHECK(results.empty());
}

TEST_CASE("find_system_topic_exact", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("$SYS/b"));
  const auto results = store.find("$SYS/b");
  CHECK(results.size() == 1U);
}

TEST_CASE("find_multiple_matches", "[store]") {
  RetainedMessageStore store;
  store.store(make_message("a/1"));
  store.store(make_message("a/2"));
  store.store(make_message("b/1"));
  const auto results = store.find("a/+");
  CHECK(results.size() == 2U);
}
