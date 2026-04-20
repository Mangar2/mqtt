#include "broker/enhanced_auth_registry.h"

namespace mqtt {

void EnhancedAuthRegistry::erase_client(std::string_view client_id) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  pending_enhanced_auth_.erase(std::string(client_id));
  active_enhanced_auth_.erase(std::string(client_id));
}

void EnhancedAuthRegistry::upsert_pending(std::string client_id,
                                          PendingEnhancedAuthContext context) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  pending_enhanced_auth_.insert_or_assign(std::move(client_id),
                                          std::move(context));
}

void EnhancedAuthRegistry::upsert_active(std::string client_id,
                                         EnhancedAuthHandler handler) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  active_enhanced_auth_.insert_or_assign(std::move(client_id),
                                         std::move(handler));
}

void EnhancedAuthRegistry::erase_pending(std::string_view client_id) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  pending_enhanced_auth_.erase(std::string(client_id));
}

void EnhancedAuthRegistry::erase_active(std::string_view client_id) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  active_enhanced_auth_.erase(std::string(client_id));
}

} // namespace mqtt
