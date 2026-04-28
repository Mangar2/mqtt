module.exports = [
    {
        description: 'Connect test',
        clientId: 'test-client',
        result: { clean: false, token: { send:'abcd', receive: 'efgh' }, present: 0, mqttcode: 0 },
        tests: [
            {
                description: 'Testing connect method, clean session',
                method: 'connect',
                args: [{ version: '1.0', clean: true, port: 0, host: 'localhost' }],
                expected: {
                    'result': {
                        clean: false,
                        present: 0,
                        mqttcode: 0,
                        token: { send: 'abcd', receive: 'efgh' }
                    },
                    'history': [
                        {
                            clean: true,
                            port: 0,
                            host: 'localhost',
                            clientId: 'test-client',
                            keepAlive: 0,
                            qos: 1,
                            version: '1.0'
                        }
                    ]
                }
            },
            {
                description: 'Testing connect, missing arg clean',
                method: 'connect',
                args: [{ version: '1.0', port: 0, host: 'localhost' }],
                expected: {
                    'result': 'Unable to connect: Unable to connect: {"clean":"missing property "}'
                }
            }
        ]
    },
    {
        description: 'Disconnect test',
        clientId: 'test-client',
        result: { clean: false, token: { send:'abcd', receive: 'efgh' }, present: 0, mqttcode: 0 },
        tests: [
            {
                description: 'Testing connect method, clean session',
                method: 'disconnect',
                args: ['1.0'],
                expected: {
                    'history': [
                        {
                            clientId: 'test-client'
                        }
                    ]
                }
            }
        ]
    }
]
