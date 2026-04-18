#include "persistence/crash_safe_file.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <ios>
#include <span>
#include <system_error>

#include "persistence/persistence_error.h"
#include "persistence/record_codec.h"

namespace mqtt {

namespace {

/// CRC-32/ISO-HDLC lookup table (reflected, polynomial 0xEDB88320).
/// Not constexpr so it is initialised at runtime and captured by coverage.
std::array<uint32_t, 256> make_crc_table() noexcept {
  std::array<uint32_t, 256> tbl{};
  for (uint32_t idx = 0U; idx < 256U; ++idx) {
    uint32_t crc = idx;
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1U) ^ 0xEDB88320U;
      } else {
        crc >>= 1U;
      }
    }
    tbl[idx] = crc;
  }
  return tbl;
}

const std::array<uint32_t, 256> k_crc_table = make_crc_table();

} // namespace

//  Construction
//

CrashSafeFile::CrashSafeFile(std::filesystem::path dir, std::string stem)
    : dir_(std::move(dir)), stem_(std::move(stem)) {}

//  Helpers
//

std::filesystem::path CrashSafeFile::path_for(std::string_view suffix) const {
  return dir_ / (stem_ + std::string(suffix));
}

//  CRC-32
//

uint32_t CrashSafeFile::crc32(std::span<const uint8_t> data) noexcept {
  uint32_t crc = 0xFFFFFFFFU;
  for (uint8_t byte : data) {
    crc =
        (crc >> 8U) ^ k_crc_table[(crc ^ static_cast<uint32_t>(byte)) & 0xFFU];
  }
  return crc ^ 0xFFFFFFFFU;
}

//  Write
//

void CrashSafeFile::write(const std::vector<uint8_t> &records,
                          uint32_t record_count) {
  // 1. Build payload: magic + version + count + records.
  std::vector<uint8_t> buf;
  buf.reserve(9U + records.size() + 4U);
  buf.insert(buf.end(), std::begin(k_magic), std::end(k_magic));
  record_codec::write_u8(buf, k_format_version);
  record_codec::write_u32(buf, record_count);
  buf.insert(buf.end(), records.begin(), records.end());

  // 2. Append CRC-32 footer.
  uint32_t crc_val = crc32(buf);
  record_codec::write_u32(buf, crc_val);

  // 3. Write to .tmp.
  auto tmp_path = path_for(".tmp");
  std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw PersistenceException(PersistenceError::WriteFailure,
                               "Cannot open temp file for writing: " +
                                   tmp_path.string());
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  out.write(reinterpret_cast<const char *>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
  if (!out) {
    throw PersistenceException(PersistenceError::WriteFailure,
                               "Write failed: " + tmp_path.string());
  }
  out.flush();
  out.close(); // explicit close before rename so the file is not held open

  // 4. Rename .dat → .bak (preserve last good copy).
  auto dat_path = path_for(".dat");
  auto bak_path = path_for(".bak");
  std::error_code err_code;
  if (std::filesystem::exists(dat_path, err_code)) {
    std::filesystem::rename(dat_path, bak_path, err_code);
    if (err_code) {
      throw PersistenceException(PersistenceError::WriteFailure,
                                 "Cannot rename .dat to .bak: " +
                                     err_code.message());
    }
  }

  // 5. Atomically promote .tmp → .dat.
  std::filesystem::rename(tmp_path, dat_path, err_code);
  if (err_code) {
    throw PersistenceException(PersistenceError::WriteFailure,
                               "Cannot rename .tmp to .dat: " +
                                   err_code.message());
  }
}

//  Read
//

std::optional<std::pair<uint32_t, std::vector<uint8_t>>>
CrashSafeFile::try_read(const std::filesystem::path &path) {
  std::error_code err_code;
  if (!std::filesystem::exists(path, err_code) || err_code) {
    return std::nullopt;
  }

  std::ifstream inp(path, std::ios::binary | std::ios::ate);
  if (!inp) {
    return std::nullopt;
  }
  auto file_size = static_cast<std::size_t>(inp.tellg());
  if (file_size < 13U) { // 4 magic + 1 version + 4 count + 4 crc minimum
    return std::nullopt;
  }
  inp.seekg(0, std::ios::beg);

  std::vector<uint8_t> raw(file_size);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  inp.read(reinterpret_cast<char *>(raw.data()),
           static_cast<std::streamsize>(file_size));
  if (!inp) {
    return std::nullopt;
  }

  // Verify CRC-32 footer (last 4 bytes).
  std::size_t payload_len = file_size - 4U;
  uint32_t stored_crc = static_cast<uint32_t>(raw[payload_len]) |
                        (static_cast<uint32_t>(raw[payload_len + 1U]) << 8U) |
                        (static_cast<uint32_t>(raw[payload_len + 2U]) << 16U) |
                        (static_cast<uint32_t>(raw[payload_len + 3U]) << 24U);
  uint32_t computed_crc =
      crc32(std::span<const uint8_t>(raw.data(), payload_len));
  if (stored_crc != computed_crc) {
    return std::nullopt;
  }

  // Verify magic.
  if (raw[0] != k_magic[0] || raw[1] != k_magic[1] || raw[2] != k_magic[2] ||
      raw[3] != k_magic[3]) {
    return std::nullopt;
  }

  // Verify format version.
  if (raw[4] != k_format_version) {
    return std::nullopt;
  }

  // Decode record count.
  uint32_t count = static_cast<uint32_t>(raw[5]) |
                   (static_cast<uint32_t>(raw[6]) << 8U) |
                   (static_cast<uint32_t>(raw[7]) << 16U) |
                   (static_cast<uint32_t>(raw[8]) << 24U);

  // Record payload starts at byte 9, ends before the CRC footer.
  std::vector<uint8_t> record_bytes(
      raw.begin() + 9, raw.begin() + static_cast<std::ptrdiff_t>(payload_len));

  return std::make_pair(count, std::move(record_bytes));
}

std::optional<std::pair<uint32_t, std::vector<uint8_t>>>
CrashSafeFile::read_latest() const {
  for (std::string_view suffix : {".dat", ".bak", ".tmp"}) {
    auto result = try_read(path_for(suffix));
    if (result.has_value()) {
      return result;
    }
  }
  return std::nullopt;
}

//  Remove
//

void CrashSafeFile::remove_all() {
  for (std::string_view suffix : {".dat", ".bak", ".tmp"}) {
    auto pth = path_for(suffix);
    std::error_code err_code;
    if (std::filesystem::exists(pth, err_code) && !err_code) {
      std::filesystem::remove(pth, err_code);
      if (err_code) {
        throw PersistenceException(PersistenceError::WriteFailure,
                                   "Cannot delete file: " + pth.string());
      }
    }
  }
}

} // namespace mqtt
