# change log for mqttclient

## 2.3.2 2023-01-14 fix

- clean = false in options is no longer ignored
- some optional parameter added to improve unit testing

## 2.3.1 2023-01-14 update

- fix for 2.3.0

## 2.3.0 2023-01-14 update

- now supporting port = 0 to select random free ports

## 2.2.0 2021-12-29 update

- corrected the use of logFilters when logging messages

## 2.1.0 2021-12-29 update

- added updateSubscriptions to allow updating subscriptions without providing a new callback

## 2.0.0 2021-12-29 update

- registerRecipient now needs a unique service name to support updating/changing subscriptions 

## 1.5.4 2020-12-11 Patch

- The memory info message now has a QoS = 0

## 1.5.3 2020-12-07 Patch

- Now copies a message and passes the copy to the service. This prevents changed messages in multi service installations

## 1.5.2 2020-11-22 Patch

- change memory usage message formatting, send memory usage every ping

## 1.5.1 2020-11-22 Patch

- changed memory usage message formatting

## 1.5.0 2020-11-22 update

- The client now sends regularily (every 100 pings) a memory usage protocol to the broker

## 1.4.0 2020-04-15 update

- Publish no longer supports qos and retain as parameter. Now it uses the qos and retain property of the message itself

## 1.2.0 2020-04-13 update

- added a chain of close functions called if close is called.

## 1.1.0 2020-04-10 update

- registerSender now supports callbacks returning single messages or message arrays
- Generated readme with js2md
- Improved inline documentation

## 1.0.0 Initial release
