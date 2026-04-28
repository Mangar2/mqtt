#include <string>
#include <windows.h>

class TinySerialImpl {
public:
    TinySerialImpl() { isOpen = false; }
    ~TinySerialImpl() {}

    /**
     * Opens a serial port
     * @param portName name of the port to open, the portnames are OS specific - for example "COM1" for Windows
     */
    bool open(const std::string portName);

    /*
    * Sets the baud rate of the com port
    * @param [9600] baudRate baudRate to set
    * @param [8] dataBits amount of bits in a byte
    * @param [ONESTOPBIT] stopBits amount of stop bits 
    * @param [NOPARITY] parity (EVENPARITY, ODDPARITY, NOPARITY)
    */
    bool TinySerialImpl::setOptions(
        DWORD baudRate = CBR_9600, 
        BYTE dataBits = 8, 
        BYTE stopBits = ONESTOPBIT, 
        BYTE parity = NOPARITY);

    /*
    * Writes data to the serial port
    * @param dataToWriteToPort a pointer to a data buffer
    * @param amountOfBytesToWrite amount of bytes to write from the buffer to the serial port
    * @returns amount of bytes written
    */
    DWORD write(const char* dataToWriteToPort, DWORD amountOfBytesToWrite);

    /*
    * Writes data to the serial port
    * @param dataToWriteToPort a string to write to the port
    * @returns amount of bytes written
    */
    DWORD write(const std::string dataToWriteToPort);

    /*
    * Reads data from the serial port
    */
    DWORD read(char* buffer, size_t bufferSize);

    /*
    * Blocks execution, until the serial port sends data
    */
    auto waitUntilDataReceived();

    /*
    * Closes the serial port
    */
    bool close();

    /*
    * Gets a string describing the error
    */
    std::string getError();

private:
    /*
     * Sets the timeout parameters to prevent blocking calls
     */
    void setTimeouts();
    
    /*
    * Sets standard com state
    */
    void setStandardComState();

    /*
    * Sets a read event - to trigger a read function once data is received
    */
    void setReadEvent();

    TinySerialImpl(const TinySerialImpl&) {};
    DWORD lastError;
    HANDLE handleToComPort;
    bool isOpen;
};
