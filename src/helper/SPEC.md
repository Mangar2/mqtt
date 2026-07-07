# src/helper — Generic String Helper Module

Reusable, domain-independent string utilities shared by broker (`mqtt`) and YAHA (`yaha`) code.

## Purpose

This module consolidates small free-function string helpers (lowercasing, trimming,
delimiter splitting) that were previously reimplemented independently in many files
across `src/broker/`, `src/yaha/`, and `src/test_client/` (see
`spec/todo/todo-duplicate-free-functions.md`). It holds only generic, non-domain-specific
string logic — no MQTT, HTTP, or YAHA-specific semantics.

## Public API

### `helper/string_helper.h` — `namespace mqtt::helper`

- `std::string toLower(std::string_view text)` — ASCII-lowercased copy.
- `std::string trim(std::string_view text)` — copy with leading/trailing `std::isspace`
  whitespace removed.
- `std::vector<std::string> split(std::string_view text, char delimiter)` — splits on a
  single delimiter character, trims each token. A trailing delimiter with nothing after
  it does not produce a trailing empty token; two consecutive delimiters produce an
  empty token between them (matches `std::getline` stream-extraction semantics, which
  the implementation uses internally).

## Out of scope

- Protocol-specific whitespace trimming (e.g. HTTP OWS-only trim used on the broker's
  WebSocket handshake hot path, `src/broker/transport/websocket_handshake.cpp`) is
  intentionally NOT unified here: it returns a non-allocating `std::string_view` and
  trims a narrower, protocol-defined character set. Forcing it onto the allocating
  `trim()` above would add an allocation to a hot parsing path and subtly change
  behavior.
- Class-scoped helpers with the same name (e.g. `ConfigLoader::trim`,
  `FileStore::toLower`, `Rs485InterfaceComponent::toLowerCopy`) are not free functions
  and are out of scope for this module.

## Usage

Callers use the fully qualified name, e.g. `mqtt::helper::trim(text)`, from both `mqtt`
and `yaha` namespaces — no `using namespace` directive.
