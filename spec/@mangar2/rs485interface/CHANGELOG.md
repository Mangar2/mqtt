# change log for rs485interface

## 1.0.1 2020-11-21 bugfix

- In stop tracing errors

## 1.0.1 2020-11-21 minor

- Added the ability to stop tracing errors

## 0.5.0 2020-11-21 update

- Removed the internal byte code of a message from the reason entry in messages as they prevent message compression
- Fix a memory leak from an address-chain constantly adding the same addresses to an array

## 0.4.1 2020-11-21 patch

- Use SerialConnection class of serialhelper module, removed local SerialConnection

## 0.4.0 2020-11-21 release

- Adjusted the reason in messages, removed the amount of bytes received

## 0.3.2 2020-11-14 patch

- Set serialport latest in package.json

## 0.3.1 2020-09-27 bugfix

- if sending to serial fails, we now wait to reopen the serial interface and retry

## 0.3.0 2020-05-19 change

- values coming from serial messages are now mapped to interface configuration string (example light "0" will be "off" now)

## 0.2.0 2020-05-04 Initial release
