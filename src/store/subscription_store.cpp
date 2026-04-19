#include "store/subscription_store.h"

#include "topic/topic_matcher.h"

namespace mqtt {

bool SubscriptionStore::store(std::string_view client_id,
                              const Subscription &sub) {
  return trie_.insert(client_id, sub);
}

bool SubscriptionStore::remove(std::string_view client_id,
                               std::string_view topic_filter) {
  return trie_.remove(client_id, topic_filter);
}

std::vector<MatchResult>
SubscriptionStore::subscribers_for(std::string_view topic_name) const {
  return TopicMatcher::match(trie_, topic_name);
}

void SubscriptionStore::remove_session(std::string_view client_id) {
  trie_.remove_all(client_id);
}

std::size_t SubscriptionStore::size() const noexcept { return trie_.size(); }

} // namespace mqtt
