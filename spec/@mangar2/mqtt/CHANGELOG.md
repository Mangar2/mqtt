# change log for mqtt

## 1.3.3 2021-12-08 Patch

- Checking port number strings to be integer

## 1.3.2 2020-12-12 Patch

- Fixed clientId no longer undefined in pingrequ log message

## 1.3.1 2020-12-12 Patch

- Added the clientId the logged path
- Refactored the log-messages

## 1.3.0 2020-12-12 Update

- Added the client-id to error messages from wrong parameters
- Adjusted input type checking to support 'true' 'false' for boolen and '0', '1', '2' for QoS (not only number)
