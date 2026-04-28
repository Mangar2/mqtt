#include <iostream>
#include "TinySerialImpl.h"

/**
 * Opens a serial port
 * @param portName name of the port to open, the portnames are OS specific - for example "COM1" for Windows
 */
bool TinySerialImpl::open(const std::string portName) {
    std::cout << portName << std::endl;
    if (isOpen) return true;
    const DWORD COM_PORT_CANNOT_BE_SHARED = 0;
    const LPSECURITY_ATTRIBUTES DEFAULT_SECURITY_DESCRIPTOR = NULL;
    const DWORD NO_FILE_ATTRIBUTES = 0;
    const HANDLE NO_TEMPLATE_FILE = NULL;

    handleToComPort = CreateFileA(portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        COM_PORT_CANNOT_BE_SHARED,
        DEFAULT_SECURITY_DESCRIPTOR,
        OPEN_EXISTING,
        NO_FILE_ATTRIBUTES,
        NO_TEMPLATE_FILE
    );
    lastError = GetLastError();
    std::cout << lastError << std::endl;
    isOpen = (handleToComPort != INVALID_HANDLE_VALUE);
    if (!isOpen) {
        lastError = GetLastError();
    }
    else {
        setTimeouts();
        setStandardComState();
        setReadEvent();
    }
    return isOpen;
}

/*
* Sets the baud rate of the com port
* @param baudRate baudRate to set
*/
bool TinySerialImpl::setOptions(DWORD baudRate, BYTE dataBits, BYTE stopBits, BYTE parity) {
    if (!isOpen) return false;
    DCB dcbSerialParams = { 0 };
    auto Status = GetCommState(handleToComPort, &dcbSerialParams);
    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = dataBits;
    dcbSerialParams.StopBits = stopBits;
    dcbSerialParams.Parity = parity;
    std::cout << "BaudRate: " << dcbSerialParams.BaudRate <<
            " ByteSize: " << (int) dcbSerialParams.ByteSize <<
            " Stop bits: " << (int) dcbSerialParams.StopBits <<
            " Parity: " << (int) dcbSerialParams.Parity << std::endl;
    bool result = SetCommState(handleToComPort, &dcbSerialParams);
    return result;
}

/*
* Writes data to the serial port
* @param dataToWriteToPort a pointer to a data buffer
* @param amountOfBytesToWrite amount of bytes to write from the buffer to the serial port
* @returns amount of bytes written
*/
DWORD TinySerialImpl::write(const char* dataToWriteToPort, DWORD amountOfBytesToWrite) {
    if (!isOpen) return 0;
    const LPOVERLAPPED NO_OVERLAP_STRUCTURE = NULL;
    DWORD amountOfBytesWritten = 0;     // No of bytes written to the port

    auto status = WriteFile(handleToComPort,
        dataToWriteToPort,
        amountOfBytesToWrite,
        &amountOfBytesWritten,
        NO_OVERLAP_STRUCTURE);
    return amountOfBytesWritten;
}

/*
* Writes data to the serial port
* @param dataToWriteToPort a string to write to the port
* @returns amount of bytes written
*/
DWORD TinySerialImpl::write(const std::string dataToWriteToPort) {
    return write(dataToWriteToPort.c_str(), (DWORD) dataToWriteToPort.length() + 1);
}

/*
* Reads data from the serial port
*/
DWORD TinySerialImpl::read(char* buffer, size_t bufferSize) {
    if (!isOpen) return 0;
    const LPOVERLAPPED NO_OVERLAP_STRUCTURE = NULL;
    DWORD amountOfBytesRead;

    ReadFile(handleToComPort,    
        buffer,       
        bufferSize,
        &amountOfBytesRead,    
        NO_OVERLAP_STRUCTURE);

    return amountOfBytesRead;
}

/*
* Blocks execution, until the serial port sends data
*/
auto TinySerialImpl::waitUntilDataReceived() {
    const LPOVERLAPPED NO_OVERLAP_STRUCTURE = NULL;
    DWORD dwEventMask;
    auto status = WaitCommEvent(handleToComPort, &dwEventMask, NO_OVERLAP_STRUCTURE);
}

/*
* Closes the serial port
*/
bool TinySerialImpl::close() {
    if (!isOpen) return true;
    bool result = CloseHandle(handleToComPort);
    isOpen = !result;
    return result;
}

/*
* Gets a string describing the error
*/
std::string TinySerialImpl::getError() {
    return std::to_string(lastError);
}

/*
 * Sets the timeout parameters to prevent blocking calls
 */
void TinySerialImpl::setTimeouts() {
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handleToComPort, &timeouts);
}

/*
* Sets standard com state
*/
void TinySerialImpl::setStandardComState() {
    if (!isOpen) return;
    DCB dcbSerialParams = { 0 };
    auto Status = GetCommState(handleToComPort, &dcbSerialParams);
    dcbSerialParams.BaudRate = CBR_9600;  
    dcbSerialParams.ByteSize = 8;         
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;  
    SetCommState(handleToComPort, &dcbSerialParams);
}

/*
* Sets a read event - to trigger a read function once data is received
*/
void TinySerialImpl::setReadEvent() {
    auto status = SetCommMask(handleToComPort, EV_RXCHAR);
}

