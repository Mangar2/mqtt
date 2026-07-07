#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "auth/anonymous_authenticator.h"
#include "broker/enhanced_auth_registry.h"

namespace mqtt {
namespace {

EnhancedAuthHandler make_auth_handler() {
  auto authenticator =
      std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  return EnhancedAuthHandler(authenticator);
}

PendingEnhancedAuthContext make_pending_context() {
  PendingEnhancedAuthContext context{
      .handler = make_auth_handler(),
      .connect_packet = ConnectPacket{},
      .close_callback = []() {},
      .assigned_client_id = std::nullopt};
  context.connect_packet.client_id = Utf8String{"pending-client"};
  return context;
}

} // namespace

TEST_CASE("enhanced_auth_registry_upsert_pending_then_erase_pending",
          "[broker]") {
  EnhancedAuthRegistry registry;
  registry.upsert_pending("client-pending", make_pending_context());

  const bool has_pending_before = registry.with_lock(
      [](auto &pending_auth_map, auto & /*active_auth_map*/) -> bool {
        return pending_auth_map.contains("client-pending");
      });
  CHECK(has_pending_before);

  registry.erase_pending("client-pending");

  const bool has_pending_after = registry.with_lock(
      [](auto &pending_auth_map, auto & /*active_auth_map*/) -> bool {
        return pending_auth_map.contains("client-pending");
      });
  CHECK_FALSE(has_pending_after);
}

TEST_CASE("enhanced_auth_registry_upsert_active_then_erase_active", "[broker]") {
  EnhancedAuthRegistry registry;
  registry.upsert_active("client-active", make_auth_handler());

  const bool has_active_before = registry.with_lock(
      [](auto & /*pending_auth_map*/, auto &active_auth_map) -> bool {
        return active_auth_map.contains("client-active");
      });
  CHECK(has_active_before);

  registry.erase_active("client-active");

  const bool has_active_after = registry.with_lock(
      [](auto & /*pending_auth_map*/, auto &active_auth_map) -> bool {
        return active_auth_map.contains("client-active");
      });
  CHECK_FALSE(has_active_after);
}

TEST_CASE("enhanced_auth_registry_erase_client_clears_pending_and_active",
          "[broker]") {
  EnhancedAuthRegistry registry;
  registry.upsert_pending("client-both", make_pending_context());
  registry.upsert_active("client-both", make_auth_handler());

  registry.erase_client("client-both");

  const bool has_entries = registry.with_lock(
      [](auto &pending_auth_map, auto &active_auth_map) -> bool {
        return pending_auth_map.contains("client-both") ||
               active_auth_map.contains("client-both");
      });
  CHECK_FALSE(has_entries);
}

} // namespace mqtt
