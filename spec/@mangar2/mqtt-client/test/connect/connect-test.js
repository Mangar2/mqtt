module.exports = [
    {
        description: 'first test',
        options: {
            clientId: 'unique',
            broker: { host: 'localhost', port: null },
            listener: 0,
            version: '1.0',
            keepAliveInSeconds: 300,
            clean: false,
            retry: 10,
            log: [{
                topic: '#',
                module: 'send',
                level: 1
            }]
        },
        tests: [
            {
                description: 'connect client',
                expected: [
                    {
                        payload: {
                            clientId: 'unique',
                            keepAlive: 300000,
                            clean: false
                        },
                        headers: {
                            'content-type': 'application/json; charset=UTF-8',
                            accept: 'application/json,text/plain',
                            'accept-charset': 'UTF-8',
                            version: '1.0',
                        },
                        path: '/connect'
                    },
                    {
                        payload: {},
                        headers: {
                            'content-type': 'application/json; charset=UTF-8',
                        },
                        path: '/pingreq'
                    }
                ]
            },
            {
                description: 'register recipient',
                recipient: { '#': 1 },
                expected: [
                    {}, {},
                    {
                        path: '/subscribe',
                        payload: {
                            clientId: 'unique',
                            topics: {
                                '#': 1
                            }
                        },
                        headers: {
                            packetid: '1',
                            version: '1.0',
                        }
                    }
                ]
            },
            {
                description: 'publish',
                publish: { topic: 'a/b', value: 'on', qos: 2 },
                expected: [
                    {}, {}, {}
                ]
            }
        ]
    },
]