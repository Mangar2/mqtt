# Abstract

Main program to run external services. Use it, if you like to connect your home automation to one of the supported services or develop your own services and add them to this main program. Supported services are (see the README of the corresponding packages for more information)

- **pushover**
- **opensensemap**
- **sunnyportal**

## Meta

- **copyright**: Copyright (c) 2020 Volker Böhm

- **author**: Volker Böhm
- **license**: This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
    "as is", without any support, and with no warranty, express or implied, as to its usefulness for
    any purpose.

## Configuration example

You need to provide a file called "yahaconfig.json" in the installation root. This configuration file is designed to contain configuration for multiple services and for multiple environments ('development', 'test', 'staging', 'production')

```javascript
{
    "development": {
        "externalServices": {
            "clientId": "yaha/external",
            "version": "1.0",
            "clean": true,
            "broker": {
                "port": 10000,
                "host": "192.168.0.1"
            },
            "listener": 10001,
            "keepAliveInSeconds": 60,
            "log": [
                {
                    "module": "all",
                    "topic" : "#",
                    "level" : 1
                },
                {
                    "module": "all",
                    "topic": "$SYS/#",
                    "level": 0
                }
            ],
            "services": [
                "opensensemap"
            ]
        }
    }
}
```

- **clientId** name of the client, must be unique across all services
- **version** version of the interface (0.0 or 1.0)
- **clean** true, if the broker shall cleanup all messages on disconnect, false to buffer messages until the service is up again. 
- **broker** information to connect to the broker
- **listener** port number to listen for broker messages, you may set any free port you like
- **keepAliveInSeconds** time between keep alive calls to the broker (usually pings)
- **log** settings to log incoming messages to the console
- **services** list of services included (currently supported: opensensemap, pushover, sunnyportal)
