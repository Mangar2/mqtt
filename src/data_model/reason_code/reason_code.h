#pragma once

/**
 * @file reason_code.h
 * @brief MQTT 5.0 Reason Codes and classification helpers (Section 2.4 / Appendix B).
 */

#include <cstdint>

namespace mqtt {

/**
 * @brief MQTT 5.0 Reason Code.
 *
 * One enumerator per distinct wire value (39 distinct values).
 * Values below 0x80 are success/informational; values from 0x80 upwards are errors.
 */
enum class ReasonCode : uint8_t {
    // ── Success / informational (0x00–0x1F) ─────────────────────────────────
    Success                 = 0x00,  ///< Also: Normal Disconnection, Granted QoS 0.
    GrantedQoS1             = 0x01,
    GrantedQoS2             = 0x02,
    DisconnectWithWill      = 0x04,
    NoMatchingSubscribers   = 0x10,
    NoSubscriptionFound     = 0x11,
    ContinueAuthentication  = 0x18,
    ReAuthenticate          = 0x19,

    // ── Errors (0x80–0xFF) ───────────────────────────────────────────────────
    UnspecifiedError                    = 0x80,
    MalformedPacket                     = 0x81,
    ProtocolError                       = 0x82,
    ImplementationSpecificError         = 0x83,
    UnsupportedProtocolVersion          = 0x84,
    ClientIdentifierNotValid            = 0x85,
    BadUserNameOrPassword               = 0x86,
    NotAuthorized                       = 0x87,
    ServerUnavailable                   = 0x88,
    ServerBusy                          = 0x89,
    Banned                              = 0x8A,
    ServerShuttingDown                  = 0x8B,
    BadAuthenticationMethod             = 0x8C,
    KeepAliveTimeout                    = 0x8D,
    SessionTakenOver                    = 0x8E,
    TopicFilterInvalid                  = 0x8F,
    TopicNameInvalid                    = 0x90,
    PacketIdentifierInUse               = 0x91,
    PacketIdentifierNotFound            = 0x92,
    ReceiveMaximumExceeded              = 0x93,
    TopicAliasInvalid                   = 0x94,
    PacketTooLarge                      = 0x95,
    MessageRateTooHigh                  = 0x96,
    QuotaExceeded                       = 0x97,
    ConnectionLost                      = 0x98,
    PayloadFormatInvalid                = 0x99,
    RetainNotSupported                  = 0x9A,
    QoSNotSupported                     = 0x9B,
    UseAnotherServer                    = 0x9C,
    ServerMoved                         = 0x9D,
    SharedSubscriptionsNotSupported     = 0x9E,
    ConnectionRateExceeded              = 0x9F,
    MaximumConnectTime                  = 0xA0,
    SubscriptionIdentifiersNotSupported = 0xA1,
    WildcardSubscriptionsNotSupported   = 0xA2,
};

/// @brief Alias for ReasonCode::Success when used as Normal Disconnection (0x00).
inline constexpr ReasonCode k_normal_disconnection = ReasonCode::Success;

/// @brief Alias for ReasonCode::Success when used as Granted QoS 0 (0x00).
inline constexpr ReasonCode k_granted_qos0         = ReasonCode::Success;

// ── Classification (Module 1.2.2) ─────────────────────────────────────────────

/**
 * @brief Returns true for success / informational codes.
 * @param code Reason code to test.
 * @return `true` if the wire value is below 0x80.
 */
[[nodiscard]] constexpr bool is_success(ReasonCode code) noexcept
{
    return static_cast<uint8_t>(code) < 0x80U;
}

/**
 * @brief Returns true for error codes.
 * @param code Reason code to test.
 * @return `true` if the wire value is 0x80 or above.
 */
[[nodiscard]] constexpr bool is_error(ReasonCode code) noexcept
{
    return static_cast<uint8_t>(code) >= 0x80U;
}

} // namespace mqtt
