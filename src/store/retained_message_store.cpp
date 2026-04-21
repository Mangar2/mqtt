#include "store/retained_message_store.h"

#include "data_model/subscription/subscription.h"
#include "topic/subscription_trie.h"
#include "topic/topic_matcher.h"

namespace mqtt {

void RetainedMessageStore::store(const Message &msg,
                                 std::chrono::steady_clock::time_point stored_at) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string &topic = msg.topic.value;
  if (msg.payload.data.empty()) {
    messages_.erase(topic);
  } else {
    messages_.insert_or_assign(topic, RetainedMessageRecord{.message = msg, .stored_at = stored_at});
  }
}

std::vector<RetainedMessageRecord>
RetainedMessageStore::find_records(std::string_view topic_filter) const {
  std::lock_guard<std::mutex> lock(mutex_);
  // Build a temporary trie containing the given filter as a single dummy
  // subscription.  TopicMatcher::match then evaluates whether each stored
  // topic name is matched by the filter.
  SubscriptionTrie temp_trie;
  Subscription dummy;
  dummy.topic_filter.value = std::string(topic_filter);
  temp_trie.insert("r", dummy);

  std::vector<RetainedMessageRecord> result;
  for (const auto &[topic, record] : messages_) {
    if (!TopicMatcher::match(temp_trie, topic).empty()) {
      result.push_back(record);
    }
  }
  return result;
}

std::vector<Message>
RetainedMessageStore::find(std::string_view topic_filter) const {
  std::vector<Message> result;
  const auto records = find_records(topic_filter);
  result.reserve(records.size());
  for (const RetainedMessageRecord &record : records) {
    result.push_back(record.message);
  }
  return result;
}

std::size_t RetainedMessageStore::size() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return messages_.size();
}

std::vector<Message> RetainedMessageStore::all() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Message> result;
  result.reserve(messages_.size());
  for (const auto &[topic, record] : messages_) {
    result.push_back(record.message);
  }
  return result;
}

} // namespace mqtt
