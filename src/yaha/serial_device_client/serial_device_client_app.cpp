#include "yaha/serial_device_client/serial_device_client_app.h"

#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/rs485_interface_client/rs485_serial_adapter.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace yaha {
namespace {

class SerialDeviceSerialTransport final : public ISerialDeviceTransport {
public:
    /**
     * @brief Opens serial transport with requested device path and baudrate.
     * @param portName Serial device path.
     * @param baudrate Serial baudrate.
     */
    void open(const std::string& portName, const std::uint32_t baudrate) override {
        serialAdapter_.open(portName, baudrate);
    }

    /**
     * @brief Closes serial transport.
     */
    void close() override {
        serialAdapter_.close();
    }

    /**
     * @brief Sends one text payload as raw bytes.
     * @param payloadText UTF-8 payload text.
     */
    void sendData(const std::string& payloadText) override {
        const std::vector<std::uint8_t> payloadBytes{payloadText.begin(), payloadText.end()};
        serialAdapter_.send(payloadBytes);
    }

    /**
     * @brief Emits deterministic placeholder output for available-port listing.
     */
    void listAvailablePorts() override {
        std::cout << "serial_device_client[info] available_ports listing is not implemented"
                  << '\n' << std::flush;
    }

    /**
     * @brief Sets receive callback for incoming serial bytes.
     * @param callbackValue Callback to invoke.
     */
    void setReceiveCallback(ReceiveCallback callbackValue) override {
        serialAdapter_.setReceiveCallback(std::move(callbackValue));
    }

    /**
     * @brief Reports open state of the wrapped serial adapter.
     * @return True when serial adapter is open.
     */
    [[nodiscard]] bool isOpen() const override {
        return serialAdapter_.isOpen();
    }

private:
    Rs485SerialAdapter serialAdapter_{};
};

} // namespace

SerialDeviceClientRuntimeObjects buildSerialDeviceClientRuntime(
    const SerialDeviceClientRuntimeConfig& runtimeConfig) {
    SerialDeviceClientRuntimeObjects output{};
    output.runtimeConfig = runtimeConfig;

    output.serialTransport = std::make_shared<SerialDeviceSerialTransport>();
    output.component = std::make_unique<SerialDeviceComponent>(
        output.runtimeConfig.serialDeviceConfig,
        output.serialTransport);

    output.mqttClient = std::make_unique<YahaMqttClient>(
        output.runtimeConfig.mqttConfig,
        *output.component,
        makeBrokerTransport());

    output.runtime = std::make_unique<YahaMqttClientRuntime>(
        *output.mqttClient,
        *output.component);

    return output;
}

} // namespace yaha
