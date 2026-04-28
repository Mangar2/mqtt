# change log for messagestore

## 2.2.0 2022-01-09 update

- added the ability to call cleanup on /$SYS/messages/cleanup

## 2.1.0 2020-12-13 minor

- added index.js providing MessageStore class and prepare function for runservices compatibility

## 2.0.1 2020-11-30 bugfix

- Correct handling of requests from old GUI without provinding a payload

## 2.0 2020-11-29 version

- Changed return message format, removed the "topics" property, as it is unused
- The "payload" property now contains an array of nodes and no longer an object of the format { topic: node } as the node already contains the topic
- Added the ability to select only a list of changed nodes. Therefor a list of nodes is required as request body

## 1.2.3 2020-07-04 patch

- Now using the reason flag. The result will not contain reason information if it is false

## 1.2.2 2020-07-04 fix

- replace '%20' coded spaces by spaces in topics. Needed on raspberry pi.

## 1.2 2020-06-30 update

- Added a configuration for message subscriptions. Change related to deprication of messageservice (use runservice now).

## 1.1.1 2020-04-12 fix

- Changed the default port for the service to 8203

## 1.1.0 2020-04-11 update

- Implemented sanitzing by JSON schema
- Added default values for configuration

## 1.0 Initial release

- Initial release
