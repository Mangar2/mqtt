#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "broker/broker_error.h"
#include "broker/config_loader.h"

namespace {
constexpr std::string_view k_version = "0.1.0";
} // namespace

int main(int argc, char *argv[]) {
  std::cout << "mqtt-broker " << k_version << '\n';
  std::cout << "Warning: ClientHandler is currently a placeholder and closes "
               "connections immediately.\n";

  // Load configuration from the first argument, or use defaults.
  mqtt::BrokerConfig config;
  if (argc >= 2) {
    const std::filesystem::path config_path{argv[1]};
    try {
      config = mqtt::ConfigLoader::load(config_path);
    } catch (const mqtt::BrokerException &exc) {
      std::cerr << "Configuration error: " << exc.what() << '\n';
      return EXIT_FAILURE;
    }
  }

  mqtt::Broker::install_signal_handlers();

  mqtt::Broker broker(config);
  try {
    broker.startup();
  } catch (const std::exception &exc) {
    std::cerr << "Startup failed: " << exc.what() << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "Broker running on MQTT port " << config.mqtt_port;
  if (config.ws_port != 0U) {
    std::cout << ", WebSocket port " << config.ws_port;
  }
  std::cout << '\n';

  // Main loop — poll for shutdown signal and tick monitoring.
  // Accept loops run on background threads started by Broker::startup().
  while (!mqtt::Broker::shutdown_requested()) {
    broker.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  broker.shutdown();
  std::cout << "Broker stopped.\n";
  return EXIT_SUCCESS;
}
