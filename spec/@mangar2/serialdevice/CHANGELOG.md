# change log for serialdevice

## 1.4.1 2022-01-12 bugfix

- uses the correct topic for trace level 

## 1.4.0 2022-01-03 update

- ensuring delay of 100 ms between two messages send to serial device now

## 1.3.0 2021-12-22 update

- bugfixes
- changed file names to lower case to better support linux

## 1.2.0 2020-12-07 update

- separated "command map" and "send map" in fs20 definition to prevent the handling of set commands for fs20 commands that can only be received (remote control commands to zwave or i2c devices)

## 1.1.0 2020-12-06 update

- changed the index.js file to only include the main object SerialDevice
- added the function prepare to index.js to comply with the new runservices requirements

## 1.0.5 2020-10-30 default configuration changes

- bugfixing

## 1.0.4 2020-10-25 default configuration changes

- bugfixing

## 1.0.2 2020-10-19 default configuration changes

- added default baud rate and default qos

## 1.0.0 2020-10-18 Initial release
