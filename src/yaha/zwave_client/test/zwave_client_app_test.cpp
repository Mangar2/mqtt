#include <catch2/catch_test_macros.hpp>

#include "httplib.h"
#include "yaha/ini/ini_document.h"
#include "yaha/zwave_client/zwave_client_app.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint16_t kOverrideNodeIdSeven = 7U;
constexpr std::uint16_t kOverrideNodeIdEight = 8U;
constexpr std::uint16_t kOverrideNodeIdNine = 9U;
constexpr std::uint16_t kOverrideClassSwitchBinary = 37U;
constexpr std::uint16_t kOverrideClassSensorMultilevel = 49U;
constexpr int kHttpStatusOk = 200;
constexpr int kHttpStatusNotFound = 404;
constexpr int kHttpStatusInternalServerError = 500;
constexpr auto kServerStartWait = std::chrono::milliseconds{20};

class FileStoreSettingsMockServer {
public:
  explicit FileStoreSettingsMockServer(const std::uint16_t port) : port_{port} {
    server_.Get("/zwave/settings",
                [this](const httplib::Request &, httplib::Response &response) {
                  response.status = getStatus_;
                  response.set_content(getPayload_, "application/json");
                });

    server_.Post("/zwave/settings", [this](const httplib::Request &request,
                                           httplib::Response &response) {
      lastPostBody_ = request.body;
      postCount_ += 1U;
      response.status = postStatus_;
      response.set_content("", "text/plain");
    });

    serverThread_ = std::thread(
        [this] { server_.listen("127.0.0.1", static_cast<int>(port_)); });

    std::this_thread::sleep_for(kServerStartWait);
  }

  ~FileStoreSettingsMockServer() {
    server_.stop();
    if (serverThread_.joinable()) {
      serverThread_.join();
    }
  }

  void setGetStatus(const int statusCode) { getStatus_ = statusCode; }

  void setPostStatus(const int statusCode) { postStatus_ = statusCode; }

  void setGetPayload(const std::string &payloadText) {
    getPayload_ = payloadText;
  }

  [[nodiscard]] std::size_t postCount() const { return postCount_; }

  [[nodiscard]] std::string lastPostBody() const { return lastPostBody_; }

private:
  httplib::Server server_{};
  std::thread serverThread_{};
  std::uint16_t port_{0U};
  int getStatus_{kHttpStatusOk};
  int postStatus_{kHttpStatusOk};
  std::string getPayload_{R"({"devices":[]})"};
  std::string lastPostBody_{};
  std::size_t postCount_{0U};
};

[[nodiscard]] std::filesystem::path makeTemporaryDirectory() {
  const auto tickValue =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path directoryPath =
      std::filesystem::temp_directory_path() /
      ("yaha_zwave_client_test_" + std::to_string(tickValue));
  std::filesystem::create_directories(directoryPath);
  return directoryPath;
}

void removeDirectoryQuiet(const std::filesystem::path &directoryPath) {
  std::error_code errorCode{};
  std::filesystem::remove_all(directoryPath, errorCode);
}

[[nodiscard]] std::filesystem::path
writeIniFile(const std::filesystem::path &directoryPath,
             const std::string &contentText) {
  const std::filesystem::path filePath = directoryPath / "zwave.ini";
  std::ofstream output{filePath};
  output << contentText;
  return filePath;
}

[[nodiscard]] yaha::IniDocument loadIni(const std::string &iniText) {
  const std::filesystem::path tempDirectory = makeTemporaryDirectory();
  const std::filesystem::path iniPath = writeIniFile(tempDirectory, iniText);
  yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);
  removeDirectoryQuiet(tempDirectory);
  return document;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_config_applies_defaults_and_parses_device",
          "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|7\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.subscribeQos == yaha::Qos::AtLeastOnce);
  CHECK(config.qos == yaha::Qos::AtLeastOnce);
  CHECK_FALSE(config.retain);
  CHECK(config.pollIntervalMs == 600000);
  CHECK(config.usb.device == "/dev/ttyUSB0");
  CHECK(config.usb.topic == "home/zwave/controller");
  REQUIRE(config.devices.size() == 1U);
  CHECK(config.devices.front().topic == "home/lamp");
  CHECK(config.devices.front().nodeId == 7U);
  CHECK_FALSE(config.devices.front().classId.has_value());
}

TEST_CASE("load_zwave_config_rejects_invalid_device_row", "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|500\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  CHECK_FALSE(loaded);
  CHECK(errorMessage.find("nodeId must be in range 1..255") !=
        std::string::npos);
}

TEST_CASE("load_zwave_config_rejects_invalid_device_field_count",
          "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  CHECK_FALSE(loaded);
  CHECK(errorMessage.find("invalid zwave.device entry") != std::string::npos);
}

TEST_CASE("load_zwave_config_rejects_empty_device_topic", "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=|7\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  CHECK_FALSE(loaded);
  CHECK(errorMessage.find("topic must not be empty") != std::string::npos);
}

TEST_CASE("load_zwave_config_rejects_invalid_optional_numeric_fields",
          "[zwave_client]") {
  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7|bad\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK_FALSE(
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.find("classId") != std::string::npos);
  }

  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7|37|300\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK_FALSE(
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.find("instance") != std::string::npos);
  }

  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7|37|1|300\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK_FALSE(
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.find("index") != std::string::npos);
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_config_falls_back_on_invalid_qos_and_retain_values",
          "[zwave_client]") {
  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "subscribeQoS=9\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.empty());
    CHECK(config.subscribeQos == yaha::Qos::AtLeastOnce);
  }

  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "qos=9\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.empty());
    CHECK(config.qos == yaha::Qos::AtLeastOnce);
  }

  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "retain=maybe\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.empty());
    CHECK_FALSE(config.retain);
  }
}

TEST_CASE("load_zwave_config_parses_log_message_flags", "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "logIncomingMessages=true\n"
                                             "logOutgoingMessages=true\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|7\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.logIncomingMessages);
  CHECK(config.logOutgoingMessages);
}

TEST_CASE("load_zwave_config_keeps_message_flags_when_log_level_is_set",
          "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "logLevel=3\n"
                                             "logIncomingMessages=true\n"
                                             "logOutgoingMessages=true\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|7\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.logLevel == 3U);
  CHECK(config.logIncomingMessages);
  CHECK(config.logOutgoingMessages);
}

TEST_CASE("load_zwave_config_keeps_message_flags_for_log_level_zero",
          "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "logLevel=0\n"
                                             "logIncomingMessages=true\n"
                                             "logOutgoingMessages=true\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|7\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.logLevel == 0U);
  CHECK(config.logIncomingMessages);
  CHECK(config.logOutgoingMessages);
}

TEST_CASE("load_zwave_config_falls_back_on_invalid_log_level",
          "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "logLevel=7\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|7\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  CHECK(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.logLevel == 2U);
}

TEST_CASE("load_zwave_config_falls_back_on_invalid_log_outgoing_messages_value",
          "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[zwave]\n"
                                             "logOutgoingMessages=maybe\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|7\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  CHECK(loaded);
  CHECK(errorMessage.empty());
  CHECK_FALSE(config.logOutgoingMessages);
}

TEST_CASE("load_zwave_config_parses_poll_interval_ms_and_falls_back_on_zero",
          "[zwave_client]") {
  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "pollIntervalMs=750\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded =
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    CHECK(errorMessage.empty());
    CHECK(config.pollIntervalMs == 750U);
  }

  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "pollIntervalMs=0\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded =
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    CHECK(loaded);
    CHECK(errorMessage.empty());
    CHECK(config.pollIntervalMs == yaha::kZwaveDefaultPollIntervalMs);
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE(
    "load_zwave_config_parses_command_reaction_timing_and_falls_back_on_zero",
    "[zwave_client]") {
  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "commandReactionPollIntervalMs=200\n"
                "commandReactionTimeoutMs=30000\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded =
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    CHECK(errorMessage.empty());
    CHECK(config.commandReactionPollIntervalMs == 200U);
    CHECK(config.commandReactionTimeoutMs == 30000U);
  }

  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "commandReactionTimeoutMs=0\n"
                "usbDevice=/dev/ttyUSB0\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded =
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    CHECK(loaded);
    CHECK(errorMessage.empty());
    CHECK(config.commandReactionTimeoutMs ==
          yaha::kZwaveDefaultCommandReactionTimeoutMs);
  }
}

TEST_CASE("load_zwave_config_requires_usb_settings", "[zwave_client]") {
  {
    const yaha::IniDocument document =
        loadIni("[zwave]\n"
                "usbTopic=home/zwave/controller\n"
                "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK_FALSE(
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.find("zwave.usbDevice") != std::string::npos);
  }

  {
    const yaha::IniDocument document = loadIni("[zwave]\n"
                                               "usbDevice=/dev/ttyUSB0\n"
                                               "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};
    CHECK_FALSE(
        yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
    CHECK(errorMessage.find("zwave.usbTopic") != std::string::npos);
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_runtime_config_combines_zwave_and_mqtt_sections",
          "[zwave_client]") {
  const yaha::IniDocument document =
      loadIni("[mqtt]\n"
              "host=broker.local\n"
              "port=1885\n"
              "clientId=zwave-client\n"
              "reconnectDelayMs=2000\n"
              "keepAliveIntervalMs=40000\n"
              "loopSleepMs=50\n"
              "logReason=false\n"
              "\n"
              "[zwave]\n"
              "subscribeQoS=2\n"
              "qos=0\n"
              "retain=true\n"
              "pollIntervalMs=750\n"
              "usbDevice=/dev/ttyUSB9\n"
              "usbTopic=home/zwave/controller\n"
              "device=home/lamp|9|37|1|0|switch|power\n");

  yaha::ZwaveClientRuntimeConfig runtimeConfig{};
  std::string errorMessage{};

  const bool loaded = yaha::tryLoadZwaveClientRuntimeConfigFromIni(
      document, runtimeConfig, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());

  CHECK(runtimeConfig.zwaveConfig.subscribeQos == yaha::Qos::ExactlyOnce);
  CHECK(runtimeConfig.zwaveConfig.qos == yaha::Qos::AtMostOnce);
  CHECK(runtimeConfig.zwaveConfig.retain);
  CHECK(runtimeConfig.zwaveConfig.pollIntervalMs == 750U);
  CHECK(runtimeConfig.zwaveConfig.usb.device == "/dev/ttyUSB9");
  CHECK(runtimeConfig.zwaveConfig.usb.topic == "home/zwave/controller");
  REQUIRE(runtimeConfig.zwaveConfig.devices.size() == 1U);
  CHECK(runtimeConfig.zwaveConfig.devices.front().topic == "home/lamp");
  CHECK(runtimeConfig.zwaveConfig.devices.front().nodeId == 9U);
  REQUIRE(runtimeConfig.zwaveConfig.devices.front().classId.has_value());
  CHECK(runtimeConfig.zwaveConfig.devices.front().classId.value() == 37U);

  CHECK(runtimeConfig.mqttConfig.brokerHost == "broker.local");
  CHECK(runtimeConfig.mqttConfig.brokerPort == 1885U);
  CHECK(runtimeConfig.mqttConfig.clientId == "zwave-client");
  CHECK(runtimeConfig.mqttConfig.reconnectDelay.count() == 2000);
  CHECK(runtimeConfig.mqttConfig.keepAliveInterval.count() == 40000);
  CHECK(runtimeConfig.mqttConfig.loopSleep.count() == 50);
  CHECK_FALSE(runtimeConfig.mqttConfig.logReason);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_config_parses_filestore_settings", "[zwave_client]") {
  const yaha::IniDocument document =
      loadIni("[filestore]\n"
              "use=true\n"
              "host=filestore.local\n"
              "port=9000\n"
              "filename=/zwave/config/settings\n"
              "topicPrefix=$MONITOR/FileStore\n"
              "startupRetryCount=9\n"
              "startupRetryIntervalSeconds=50\n"
              "\n"
              "[zwave]\n"
              "usbDevice=/dev/ttyUSB9\n"
              "usbTopic=home/zwave/controller\n"
              "device=home/lamp|9|37|1|0|switch|power\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.fileStoreEnabled);
  CHECK(config.fileStoreHost == "filestore.local");
  CHECK(config.fileStorePort == 9000U);
  CHECK(config.settingsKeyPath == "/zwave/config/settings");
  CHECK(config.fileStoreMonitorTopicPrefix == "$MONITOR/FileStore");
  CHECK(config.fileStoreStartupRetryCount == 9U);
  CHECK(config.fileStoreStartupRetryIntervalSeconds == 50U);
}

TEST_CASE("load_zwave_config_parses_filestore_monitor_topic_prefix",
          "[zwave_client]") {
  const yaha::IniDocument document =
      loadIni("[filestore]\n"
              "topicPrefix=$MONITOR/custom/filestore\n"
              "\n"
              "[zwave]\n"
              "usbDevice=/dev/ttyUSB9\n"
              "usbTopic=home/zwave/controller\n"
              "device=home/lamp|9\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.fileStoreMonitorTopicPrefix == "$MONITOR/custom/filestore");
}

TEST_CASE("load_zwave_config_falls_back_on_invalid_filestore_retry_interval",
          "[zwave_client]") {
  const yaha::IniDocument document =
      loadIni("[filestore]\n"
              "startupRetryIntervalSeconds=0\n"
              "\n"
              "[zwave]\n"
              "usbDevice=/dev/ttyUSB9\n"
              "usbTopic=home/zwave/controller\n"
              "device=home/lamp|9|37|1|0|switch|power\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  CHECK(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.fileStoreStartupRetryIntervalSeconds ==
        yaha::kZwaveDefaultFileStoreStartupRetryIntervalSeconds);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE(
    "apply_zwave_device_settings_from_json_overrides_ini_nodes_completely",
    "[zwave_client]") {
  yaha::ZwaveConfig config{};
  config.devices = {
      yaha::ZwaveDeviceConfig{
          .topic = "ini/node7/switch",
          .nodeId = kOverrideNodeIdSeven,
          .classId = std::optional<std::uint16_t>{kOverrideClassSwitchBinary},
          .instance = std::optional<std::uint8_t>{1U},
          .index = std::optional<std::uint8_t>{0U},
          .type = std::optional<std::string>{"switch"},
          .label = std::optional<std::string>{"power"}},
      yaha::ZwaveDeviceConfig{
          .topic = "ini/node7/sensor",
          .nodeId = kOverrideNodeIdSeven,
          .classId =
              std::optional<std::uint16_t>{kOverrideClassSensorMultilevel},
          .instance = std::optional<std::uint8_t>{1U},
          .index = std::optional<std::uint8_t>{1U},
          .type = std::optional<std::string>{"number"},
          .label = std::optional<std::string>{"temperature"}},
      yaha::ZwaveDeviceConfig{
          .topic = "ini/node8/switch",
          .nodeId = kOverrideNodeIdEight,
          .classId = std::optional<std::uint16_t>{kOverrideClassSwitchBinary},
          .instance = std::optional<std::uint8_t>{1U},
          .index = std::optional<std::uint8_t>{0U},
          .type = std::optional<std::string>{"switch"},
          .label = std::optional<std::string>{"power"}}};

  const std::string jsonText =
      "{\"devices\":["
      "{\"topic\":\"store/node7/"
      "replacement\",\"nodeId\":7,\"classId\":39,\"instance\":1,\"index\":0,"
      "\"type\":\"list\",\"label\":\"mode\"},"
      "{\"topic\":\"store/node9/new\",\"nodeId\":9}"
      "]}";

  std::string errorMessage{};
  const bool applied =
      yaha::tryApplyZwaveDeviceSettingsFromJson(jsonText, config, errorMessage);

  REQUIRE(applied);
  CHECK(errorMessage.empty());
  REQUIRE(config.devices.size() == 3U);

  CHECK(config.devices[0].topic == "ini/node8/switch");
  CHECK(config.devices[0].nodeId == kOverrideNodeIdEight);
  CHECK(config.devices[1].topic == "store/node7/replacement");
  CHECK(config.devices[1].nodeId == kOverrideNodeIdSeven);
  CHECK(config.devices[2].topic == "store/node9/new");
  CHECK(config.devices[2].nodeId == kOverrideNodeIdNine);
}

TEST_CASE("apply_zwave_device_settings_from_json_rejects_unknown_root_keys",
          "[zwave_client]") {
  yaha::ZwaveConfig config{};
  config.devices = {yaha::ZwaveDeviceConfig{.topic = "ini/node7/switch",
                                            .nodeId = kOverrideNodeIdSeven}};

  const std::string jsonText =
      R"({"devices":[{"topic":"store/node7","nodeId":7}],"qos":1})";

  std::string errorMessage{};
  const bool applied =
      yaha::tryApplyZwaveDeviceSettingsFromJson(jsonText, config, errorMessage);

  CHECK_FALSE(applied);
  CHECK(errorMessage.find("unknown root key") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serialize_zwave_settings_to_json_writes_only_devices_root",
          "[zwave_client]") {
  yaha::ZwaveConfig config{};
  config.subscribeQos = yaha::Qos::ExactlyOnce;
  config.qos = yaha::Qos::AtMostOnce;
  config.retain = true;
  config.logLevel = 4U;
  config.fileStoreEnabled = true;
  config.fileStoreHost = "filestore.example";
  config.settingsKeyPath = "/zwave/custom";
  config.usb.device = "/dev/ttyUSB7";
  config.usb.topic = "$SYS/zwave/usb";
  config.devices = {yaha::ZwaveDeviceConfig{
      .topic = "store/node7/switch",
      .nodeId = kOverrideNodeIdSeven,
      .classId = std::optional<std::uint16_t>{kOverrideClassSwitchBinary}}};

  const std::string jsonText = yaha::serializeZwaveSettingsToJson(config);

  CHECK(jsonText.find("\"devices\"") != std::string::npos);
  CHECK(jsonText.find("\"topic\":\"store/node7/switch\"") != std::string::npos);
  CHECK(jsonText.find("\"nodeId\":7") != std::string::npos);
  CHECK(jsonText.find("\"qos\"") == std::string::npos);
  CHECK(jsonText.find("\"usb\"") == std::string::npos);
  CHECK(jsonText.find("\"settingsKeyPath\"") == std::string::npos);
  CHECK(jsonText.find("\"fileStoreHost\"") == std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_config_parses_legacy_json_equivalent_device_rows",
          "[zwave_client]") {
  const yaha::IniDocument document =
      loadIni("[zwave]\n"
              "usbDevice=/dev/zwave\n"
              "usbTopic=$SYS/zwave/usb stick\n"
              "device=first/dressingroom/zwave/sys/dressing room|24\n"
              "device=ground/livingroom/zwave/shutter/southwest|13|38\n"
              "device=first/study/zwave/sys/pcvolker|22||2\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.usb.device == "/dev/zwave");
  CHECK(config.usb.topic == "$SYS/zwave/usb stick");
  REQUIRE(config.devices.size() == 3U);

  CHECK(config.devices[0].topic ==
        "first/dressingroom/zwave/sys/dressing room");
  CHECK(config.devices[0].nodeId == 24U);
  CHECK_FALSE(config.devices[0].classId.has_value());

  CHECK(config.devices[1].topic == "ground/livingroom/zwave/shutter/southwest");
  REQUIRE(config.devices[1].classId.has_value());
  CHECK(config.devices[1].classId.value() == 38U);
  CHECK_FALSE(config.devices[1].type.has_value());

  CHECK(config.devices[2].topic == "first/study/zwave/sys/pcvolker");
  REQUIRE(config.devices[2].instance.has_value());
  CHECK(config.devices[2].instance.value() == 2U);
}

TEST_CASE("load_zwave_runtime_config_falls_back_on_invalid_mqtt_values",
          "[zwave_client]") {
  const yaha::IniDocument document = loadIni("[mqtt]\n"
                                             "port=70000\n"
                                             "\n"
                                             "[zwave]\n"
                                             "usbDevice=/dev/ttyUSB0\n"
                                             "usbTopic=home/zwave/controller\n"
                                             "device=home/lamp|7\n");

  yaha::ZwaveClientRuntimeConfig runtimeConfig{};
  std::string errorMessage{};

  const bool loaded = yaha::tryLoadZwaveClientRuntimeConfigFromIni(
      document, runtimeConfig, errorMessage);

  CHECK(loaded);
  CHECK(errorMessage.empty());
  CHECK(runtimeConfig.mqttConfig.brokerPort ==
        yaha::YahaMqttClient::k_default_broker_port);
}

TEST_CASE("load_zwave_config_allows_missing_device_setting", "[zwave_client]") {
  const yaha::IniDocument document =
      loadIni("[zwave]\n"
              "usbDevice=/dev/ttyUSB0\n"
              "usbTopic=home/zwave/controller\n");

  yaha::ZwaveConfig config{};
  std::string errorMessage{};

  const bool loaded =
      yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(config.devices.empty());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("sync_zwave_settings_from_filestore_replaces_snapshot_and_persists",
          "[zwave_client]") {
  constexpr std::uint16_t mockPort = 18490U;
  FileStoreSettingsMockServer fileStore{mockPort};
  fileStore.setGetPayload(
      R"({"devices":[{"topic":"store/node7/replacement","nodeId":7,"classId":39,"instance":1,"index":0,"type":"list","label":"mode"}]})");

  yaha::ZwaveConfig config{};
  config.fileStoreEnabled = true;
  config.fileStoreHost = "127.0.0.1";
  config.fileStorePort = mockPort;
  config.settingsKeyPath = "/zwave/settings";
  config.devices = {
      yaha::ZwaveDeviceConfig{
          .topic = "ini/node7/switch",
          .nodeId = kOverrideNodeIdSeven,
          .classId = std::optional<std::uint16_t>{kOverrideClassSwitchBinary}},
      yaha::ZwaveDeviceConfig{
          .topic = "ini/node8/switch",
          .nodeId = kOverrideNodeIdEight,
          .classId = std::optional<std::uint16_t>{kOverrideClassSwitchBinary}}};

  std::string errorMessage{};
  const bool synced =
      yaha::trySyncZwaveDeviceSettingsFromFileStore(config, errorMessage);

  REQUIRE(synced);
  CHECK(errorMessage.empty());
  REQUIRE(config.devices.size() == 1U);
  CHECK(config.devices[0].nodeId == kOverrideNodeIdSeven);
  CHECK(config.devices[0].topic == "store/node7/replacement");
  CHECK(fileStore.postCount() >= 1U);
  CHECK(fileStore.lastPostBody().find("\"devices\"") != std::string::npos);
}

TEST_CASE("sync_zwave_settings_from_filestore_returns_true_when_disabled",
          "[zwave_client]") {
  yaha::ZwaveConfig config{};
  config.fileStoreEnabled = false;
  config.devices = {yaha::ZwaveDeviceConfig{.topic = "ini/node8/switch",
                                            .nodeId = kOverrideNodeIdEight}};

  std::string errorMessage{};
  const bool synced =
      yaha::trySyncZwaveDeviceSettingsFromFileStore(config, errorMessage);

  REQUIRE(synced);
  CHECK(errorMessage.empty());
  REQUIRE(config.devices.size() == 1U);
  CHECK(config.devices[0].nodeId == kOverrideNodeIdEight);
}

TEST_CASE("sync_zwave_settings_from_filestore_reports_load_failure",
          "[zwave_client]") {
  constexpr std::uint16_t mockPort = 18491U;
  FileStoreSettingsMockServer fileStore{mockPort};
  fileStore.setGetStatus(kHttpStatusInternalServerError);

  yaha::ZwaveConfig config{};
  config.fileStoreEnabled = true;
  config.fileStoreHost = "127.0.0.1";
  config.fileStorePort = mockPort;
  config.settingsKeyPath = "/zwave/settings";

  std::string errorMessage{};
  const bool synced =
      yaha::trySyncZwaveDeviceSettingsFromFileStore(config, errorMessage);

  CHECK_FALSE(synced);
  CHECK(errorMessage.find("filestore") != std::string::npos);
}

TEST_CASE(
    "load_zwave_device_settings_snapshot_from_filestore_handles_deleted_key",
    "[zwave_client]") {
  constexpr std::uint16_t mockPort = 18492U;
  FileStoreSettingsMockServer fileStore{mockPort};
  fileStore.setGetStatus(kHttpStatusNotFound);

  yaha::ZwaveConfig config{};
  config.fileStoreEnabled = true;
  config.fileStoreHost = "127.0.0.1";
  config.fileStorePort = mockPort;
  config.settingsKeyPath = "/zwave/settings";
  config.devices = {yaha::ZwaveDeviceConfig{.topic = "ini/node8/switch",
                                            .nodeId = kOverrideNodeIdEight}};

  std::vector<yaha::ZwaveDeviceConfig> loadedDevices{};
  std::string errorMessage{};
  const bool loaded = yaha::tryLoadZwaveDeviceSettingsSnapshotFromFileStore(
      config, loadedDevices, errorMessage);

  REQUIRE(loaded);
  CHECK(errorMessage.empty());
  CHECK(loadedDevices.empty());
}
