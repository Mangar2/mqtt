#pragma once

/**
 * @file file_store_client_app.h
 * @brief Runtime config types and mapping helpers for YAHA FileStore standalone process.
 */

#include "yaha/file_store/file_store.h"
#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime configuration for FileStore standalone process.
 */
struct FileStoreClientRuntimeConfig {
    FileStoreConfig storeConfig{};    ///< FileStore domain configuration.
    YahaMqttClient::Config mqttConfig{}; ///< MQTT runtime configuration.
};

/**
 * @brief Result for FileStore config loading.
 */
struct FileStoreConfigLoadResult {
    bool success{false};         ///< True when loading succeeded.
    FileStoreConfig config{};    ///< Parsed config values when success is true.
    std::string errorMessage{};  ///< Human-readable error text when success is false.
};

/**
 * @brief Result for FileStore client runtime config loading.
 */
struct FileStoreClientRuntimeConfigLoadResult {
    bool success{false};                     ///< True when loading succeeded.
    FileStoreClientRuntimeConfig config{};   ///< Parsed runtime config when success is true.
    std::string errorMessage{};              ///< Human-readable error text when success is false.
};

/**
 * @brief Maps FileStore domain config from parsed INI document.
 * @param document Parsed INI document.
 * @return FileStore config load result.
 */
[[nodiscard]] FileStoreConfigLoadResult loadFileStoreConfigFromIni(const IniDocument& document);

/**
 * @brief Maps runtime configuration from parsed INI document.
 * @param document Parsed INI document.
 * @return FileStore client runtime config load result.
 */
[[nodiscard]] FileStoreClientRuntimeConfigLoadResult
loadFileStoreClientRuntimeConfigFromIni(const IniDocument& document);

} // namespace yaha
