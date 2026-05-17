#include "yaha/rs485_interface_client/rs485_serial_adapter.h"

#include <cerrno>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace yaha {
namespace {

constexpr std::uint32_t k_baudrate_1200{1200U};
constexpr std::uint32_t k_baudrate_2400{2400U};
constexpr std::uint32_t k_baudrate_4800{4800U};
constexpr std::uint32_t k_baudrate_9600{9600U};
constexpr std::uint32_t k_baudrate_19200{19200U};
constexpr std::uint32_t k_baudrate_38400{38400U};
constexpr std::uint32_t k_baudrate_57600{57600U};
constexpr std::uint32_t k_baudrate_115200{115200U};
constexpr std::uint32_t k_baudrate_230400{230400U};

#if !defined(_WIN32)
[[nodiscard]] speed_t mapBaudrate(const std::uint32_t baudrate) {
    switch (baudrate) {
        case k_baudrate_1200:
            return B1200;
        case k_baudrate_2400:
            return B2400;
        case k_baudrate_4800:
            return B4800;
        case k_baudrate_9600:
            return B9600;
        case k_baudrate_19200:
            return B19200;
        case k_baudrate_38400:
            return B38400;
        case k_baudrate_57600:
            return B57600;
#ifdef B115200
        case k_baudrate_115200:
            return B115200;
#endif
#ifdef B230400
        case k_baudrate_230400:
            return B230400;
#endif
        default:
            return B57600;
    }
}
#endif

} // namespace

Rs485SerialAdapter::~Rs485SerialAdapter() {
    close();
}

bool Rs485SerialAdapter::open(
    const std::string& portName,
    const std::uint32_t baudrate,
    std::string& errorMessage) {
    close();

#if defined(_WIN32)
    (void)portName;
    (void)baudrate;
    errorMessage = "RS485 serial adapter is not implemented on Windows in this build";
    return false;
#else
    const int descriptor = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (descriptor < 0) {
        errorMessage = "failed to open serial port '" + portName + "': " + std::strerror(errno);
        return false;
    }

    struct termios ttySettings {};
    if (::tcgetattr(descriptor, &ttySettings) != 0) {
        errorMessage = "failed to read serial attributes: " + std::string{std::strerror(errno)};
        ::close(descriptor);
        return false;
    }

    ::cfmakeraw(&ttySettings);
    const speed_t speed = mapBaudrate(baudrate);
    if (::cfsetispeed(&ttySettings, speed) != 0 || ::cfsetospeed(&ttySettings, speed) != 0) {
        errorMessage = "failed to apply serial baudrate: " + std::string{std::strerror(errno)};
        ::close(descriptor);
        return false;
    }

    ttySettings.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    ttySettings.c_cflag &= static_cast<tcflag_t>(~PARENB);
    ttySettings.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    ttySettings.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    ttySettings.c_cflag |= CS8;
    ttySettings.c_cc[VMIN] = 0;
    ttySettings.c_cc[VTIME] = 1;

    if (::tcsetattr(descriptor, TCSANOW, &ttySettings) != 0) {
        errorMessage = "failed to set serial attributes: " + std::string{std::strerror(errno)};
        ::close(descriptor);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock{ioMutex_};
        fileDescriptor_ = descriptor;
    }

    running_.store(true);
    readThread_ = std::thread([this]() {
        readLoop();
    });

    return true;
#endif
}

void Rs485SerialAdapter::close() {
    running_.store(false);

    if (readThread_.joinable()) {
        readThread_.join();
    }

#if !defined(_WIN32)
    int descriptor = -1;
    {
        std::lock_guard<std::mutex> lock{ioMutex_};
        descriptor = fileDescriptor_;
        fileDescriptor_ = -1;
    }

    if (descriptor >= 0) {
        ::close(descriptor);
    }
#endif
}

bool Rs485SerialAdapter::send(const std::vector<std::uint8_t>& payload, std::string& errorMessage) {
#if defined(_WIN32)
    (void)payload;
    errorMessage = "send is not implemented on Windows in this build";
    return false;
#else
    int descriptor = -1;
    {
        std::lock_guard<std::mutex> lock{ioMutex_};
        descriptor = fileDescriptor_;
    }

    if (descriptor < 0) {
        errorMessage = "serial interface is not open";
        return false;
    }

    std::size_t bytesSent = 0U;
    while (bytesSent < payload.size()) {
        const ssize_t written = ::write(
            descriptor,
            payload.data() + bytesSent,
            static_cast<size_t>(payload.size() - bytesSent));

        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            errorMessage = "failed to write serial data: " + std::string{std::strerror(errno)};
            return false;
        }

        bytesSent += static_cast<std::size_t>(written);
    }

    return true;
#endif
}

void Rs485SerialAdapter::setReceiveCallback(ReceiveCallback callback) {
    std::lock_guard<std::mutex> lock{callbackMutex_};
    receiveCallback_ = std::move(callback);
}

bool Rs485SerialAdapter::isOpen() const noexcept {
#if defined(_WIN32)
    return false;
#else
    std::lock_guard<std::mutex> lock{ioMutex_};
    return fileDescriptor_ >= 0;
#endif
}

void Rs485SerialAdapter::readLoop() {
#if defined(_WIN32)
    return;
#else
    constexpr std::size_t k_read_buffer_size{512U};
    std::vector<std::uint8_t> buffer(k_read_buffer_size, 0U);

    while (running_.load()) {
        int descriptor = -1;
        {
            std::lock_guard<std::mutex> lock{ioMutex_};
            descriptor = fileDescriptor_;
        }

        if (descriptor < 0) {
            break;
        }

        const ssize_t readCount = ::read(descriptor, buffer.data(), buffer.size());
        if (readCount <= 0) {
            continue;
        }

        std::vector<std::uint8_t> chunk{};
        chunk.insert(chunk.end(), buffer.begin(), buffer.begin() + readCount);

        ReceiveCallback callback{};
        {
            std::lock_guard<std::mutex> lock{callbackMutex_};
            callback = receiveCallback_;
        }

        if (static_cast<bool>(callback)) {
            callback(chunk);
        }
    }
#endif
}

} // namespace yaha
