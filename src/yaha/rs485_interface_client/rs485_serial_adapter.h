#pragma once

/**
 * @file rs485_serial_adapter.h
 * @brief POSIX serial adapter for RS485 client runtime.
 */

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yaha {

/**
 * @brief Basic serial port adapter used by RS485 client runtime.
 */
class Rs485SerialAdapter {
public:
    using ReceiveCallback = std::function<void(const std::vector<std::uint8_t>&)>;

    Rs485SerialAdapter() = default;
    ~Rs485SerialAdapter();

    Rs485SerialAdapter(const Rs485SerialAdapter&) = delete;
    Rs485SerialAdapter& operator=(const Rs485SerialAdapter&) = delete;

    /**
     * @brief Opens serial port and starts read loop.
     * @param portName Device path.
     * @param baudrate Baudrate.
        * @throws YahaError on failure.
     */
        void open(const std::string& portName, std::uint32_t baudrate);

    /**
     * @brief Closes serial port and stops read loop.
     */
    void close();

    /**
     * @brief Sends one binary frame to serial interface.
     * @param payload Bytes to send.
        * @throws YahaError on failure.
     */
        void send(const std::vector<std::uint8_t>& payload);

    /**
     * @brief Sets receive callback for read-loop data chunks.
     * @param callback Receive callback.
     */
    void setReceiveCallback(ReceiveCallback callback);

    /**
     * @brief Returns open state.
     * @return True when serial descriptor is open.
     */
    [[nodiscard]] bool isOpen() const noexcept;

private:
    void readLoop();

    int fileDescriptor_{-1};
    std::atomic<bool> running_{false};
    std::thread readThread_{};

    mutable std::mutex ioMutex_{};
    mutable std::mutex callbackMutex_{};
    ReceiveCallback receiveCallback_{};
};

} // namespace yaha
