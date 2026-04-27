#include "client/connection_negotiator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "codec/packet/connect_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/property/property_id.h"
#include "data_model/types/integers.h"
#include "data_model/types/utf8_string.h"
#include "network/stream_buffer.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mqtt {

namespace {

#ifdef _WIN32
[[nodiscard]] SOCKET to_socket(SocketHandle socket_handle) noexcept {
  return static_cast<SOCKET>(socket_handle);
}

void close_socket_handle(SocketHandle socket_handle) noexcept {
  (void)::closesocket(to_socket(socket_handle));
}
#else
[[nodiscard]] int to_fd(SocketHandle socket_handle) noexcept {
  return static_cast<int>(socket_handle);
}

void close_socket_handle(SocketHandle socket_handle) noexcept {
  (void)::close(to_fd(socket_handle));
}
#endif

[[nodiscard]] uint16_t find_receive_maximum(const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::ReceiveMaximum) {
      continue;
    }
    if (const auto *receive_maximum_ptr =
            std::get_if<TwoByteInteger>(&property.value);
        receive_maximum_ptr != nullptr) {
      return *receive_maximum_ptr;
    }
  }
  return 65535U;
}

[[nodiscard]] uint16_t
find_topic_alias_maximum(const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::TopicAliasMaximum) {
      continue;
    }
    if (const auto *topic_alias_ptr =
            std::get_if<TwoByteInteger>(&property.value);
        topic_alias_ptr != nullptr) {
      return *topic_alias_ptr;
    }
  }
  return 0U;
}

[[nodiscard]] std::optional<uint16_t>
find_server_keep_alive(const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::ServerKeepAlive) {
      continue;
    }
    if (const auto *keep_alive_ptr =
            std::get_if<TwoByteInteger>(&property.value);
        keep_alive_ptr != nullptr) {
      return *keep_alive_ptr;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
find_assigned_client_id(const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::AssignedClientIdentifier) {
      continue;
    }
    if (const auto *client_id_ptr = std::get_if<Utf8String>(&property.value);
        client_id_ptr != nullptr) {
      return client_id_ptr->value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] ConnectionNegotiationResult
build_result(const ConnackPacket &connack_packet) {
  ConnectionNegotiationResult result;
  result.session_present = connack_packet.session_present;
  result.reason_code = connack_packet.reason_code;
  result.receive_maximum = find_receive_maximum(connack_packet.properties);
  result.topic_alias_maximum =
      find_topic_alias_maximum(connack_packet.properties);
  result.server_keep_alive = find_server_keep_alive(connack_packet.properties);
  result.assigned_client_id =
      find_assigned_client_id(connack_packet.properties);
  result.connack_properties = connack_packet.properties;
  return result;
}

} // namespace

TcpConnection ConnectionNegotiator::dial_tcp(std::string_view host,
                                             uint16_t port) {
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *address_list = nullptr;
  const std::string service = std::to_string(port);
  const int resolve_status =
      ::getaddrinfo(std::string(host).c_str(), service.c_str(), &hints,
                    &address_list);
  if (resolve_status != 0 || address_list == nullptr) {
    throw ClientException(ClientError::ResolveFailed,
                          "failed to resolve broker address");
  }

  SocketHandle connected_socket = k_invalid_socket;

  for (struct addrinfo *entry = address_list; entry != nullptr;
       entry = entry->ai_next) {
#ifdef _WIN32
    const SOCKET candidate_socket =
        ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (candidate_socket == INVALID_SOCKET) {
      continue;
    }
    const SocketHandle socket_handle = static_cast<SocketHandle>(candidate_socket);
    const int connect_status =
        ::connect(candidate_socket, entry->ai_addr,
                  static_cast<int>(entry->ai_addrlen));
#else
    const int candidate_socket =
        ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (candidate_socket < 0) {
      continue;
    }
    const SocketHandle socket_handle = static_cast<SocketHandle>(candidate_socket);
    const int connect_status =
        ::connect(candidate_socket, entry->ai_addr, entry->ai_addrlen);
#endif

    if (connect_status == 0) {
      connected_socket = socket_handle;
      break;
    }

    close_socket_handle(socket_handle);
  }

  ::freeaddrinfo(address_list);

  if (connected_socket == k_invalid_socket) {
    throw ClientException(ClientError::ConnectFailed,
                          "failed to connect to broker endpoint");
  }

  return TcpConnection(connected_socket);
}

ConnectionNegotiationResult ConnectionNegotiator::negotiate(
    TcpConnection &connection, const ConnectPacket &connect_packet,
    uint32_t read_timeout_ms) {
  WriteBuffer connect_frame;
  encode_connect(connect_frame, connect_packet);

  if (!connection.write(std::span<const uint8_t>(connect_frame.data(),
                                                 connect_frame.size()))) {
    throw ClientException(ClientError::WriteFailed,
                          "failed to send CONNECT packet");
  }

  connection.set_receive_timeout(read_timeout_ms);

  StreamBuffer stream_buffer;
  std::array<uint8_t, 2048> read_chunk{};

  while (true) {
    if (stream_buffer.has_complete_packet()) {
      const std::vector<uint8_t> packet_bytes = stream_buffer.consume_packet();
      ReadBuffer packet_reader(
          std::span<const uint8_t>(packet_bytes.data(), packet_bytes.size()));
      const AnyPacket packet = read_packet(packet_reader);
      if (!std::holds_alternative<ConnackPacket>(packet)) {
        throw ClientException(
            ClientError::ProtocolError,
            "expected CONNACK as first response after CONNECT");
      }

      const ConnackPacket &connack_packet = std::get<ConnackPacket>(packet);
      if (is_error(connack_packet.reason_code)) {
        throw ClientException(
            ClientError::NegotiationRejected,
            "broker rejected CONNECT request", connack_packet.reason_code);
      }

      return build_result(connack_packet);
    }

    const std::ptrdiff_t bytes_read =
        connection.read(std::span<uint8_t>(read_chunk.data(), read_chunk.size()));
    if (bytes_read > 0) {
      const auto append_result = stream_buffer.append(
          std::span<const uint8_t>(read_chunk.data(),
                                   static_cast<std::size_t>(bytes_read)));
      if (append_result != StreamBufferAppendResult::kOk) {
        throw ClientException(
            ClientError::ProtocolError,
            "received response exceeded stream buffer limits");
      }
      continue;
    }

    if (bytes_read == 0) {
      throw ClientException(ClientError::ReadFailed,
                            "broker closed socket during CONNECT negotiation");
    }

    if (connection.last_read_timed_out()) {
      throw ClientException(ClientError::Timeout,
                            "timed out while waiting for CONNACK");
    }

    throw ClientException(ClientError::ReadFailed,
                          "socket read failed during CONNECT negotiation");
  }
}

} // namespace mqtt
