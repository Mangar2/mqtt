#pragma once

/**
 * @file persistence_error.h
 * @brief Error codes and exception type for the persistence module (Module 13).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes produced by the persistence adapter (Module 13).
 */
enum class PersistenceError : uint8_t {
  WriteFailure = 0,     ///< Could not write or rename a file.
  ReadFailure = 1,      ///< Could not open or read a file.
  ChecksumMismatch = 2, ///< CRC-32 footer does not match file content.
  MagicMismatch = 3,    ///< File does not start with the expected magic bytes.
  VersionUnsupported = 4, ///< File format version is not supported.
  CorruptRecord = 5,      ///< A record contains out-of-range or malformed data.
};

/**
 * @brief Exception thrown by all persistence adapter operations on failure.
 *
 * Carries the originating @p PersistenceError code and a human-readable
 * description.  Callers may catch this type to distinguish persistence failures
 * from other runtime errors.
 */
class PersistenceException : public std::runtime_error {
public:
  /**
   * @brief Construct a PersistenceException.
   * @param err  Classification of the failure.
   * @param msg  Human-readable description of the failure.
   */
  explicit PersistenceException(PersistenceError err, const std::string &msg)
      : std::runtime_error(msg), error_(err) {}

  /**
   * @brief Return the error code that caused this exception.
   * @return PersistenceError classification.
   */
  [[nodiscard]] PersistenceError error() const noexcept { return error_; }

private:
  PersistenceError error_; ///< Classification of the failure.
};

} // namespace mqtt
