# change log for httpservice

## 2.2.0 2023-04-12 feature

- Sends 404 for unsupported methods

## 2.1.0 2023-02-08 features

- Agent support 
- echo-server for testing

## 2.0.3 2023-01-14 fix

- prints the right port number on listen to stdout

## 2.0.2 2023-01-14 fix

-address returns undefined now, if not yet available

## 2.0.1 2023-01-14 fix

- removed node: prefixes as they do not work with older nodejs versions

## 2.0.0 2023-01-14 release

- Adds a new interface having the parameter in an object
- Supports type based coding of the content for json, url-encoded body, text and html

## 1.2.0 2023-01-09 update

- Added shortcut method put, post, get to httpclient and httpsclient

## 1.1.2 2022-11-13 update

- Fixed a bug calculating the false length to send for characters having more than one byte in utf-8

## 1.1.1 2022-11-13 update

- Added 'options' as supported http call

## 1.1.0 2020-04-14 update

- Replaced deprecated parse(url)
- The get callback now returns URLSearchParams as first parameter (no longer payload)

## 1.0.0 Initial release

- Includes services Automation & RemoteService
