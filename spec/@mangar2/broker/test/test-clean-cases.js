module.exports = [
    {
        description: 'Test cases for MQTT with clean session flag set to true',
        connect: [
            {
                clientId: 'clientClean1', host: 'hostC1', port: 'portC1', clean: true, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'clientClean2', host: 'hostC2', port: 'portC2', clean: true, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'clientClean1', topic: { 'topic/clean': 1 } }
        ],
        tests: [
            {
                description: 'publish to topic/clean and expect clientClean1 to receive it',
                publish: [
                    { topic: 'topic/clean', value: 'valueClean', qos: 1, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topic/clean', value: 'valueClean', qos: 1 }, clientId: 'clientClean1', status: 'sending' }
                    ]
                ]
            },
            {
                description: 'Disconnect clientClean1',
                disconnect: ['clientClean1'],
                expected: []
            },
            {
                description: 'Reconnect clientClean1 and clientClean2 subscribes to topic/clean',
                connect: [
                    { clientId: 'clientClean1', host: 'hostC1', port: 'portC1', clean: true, version: '1.0', keepAlive: 100 }
                ],
                subscribe: [
                    { client: 'clientClean2', topic: { 'topic/clean': 1 } }
                ],
                expected: []
            },
            {
                description: 'publish to topic/clean after clientClean1 reconnected with clean session, expect only clientClean2 to receive it',
                publish: [
                    { topic: 'topic/clean', value: 'valueCleanReconnect', qos: 1, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topic/clean', value: 'valueCleanReconnect', qos: 1 }, clientId: 'clientClean2', status: 'sending' }
                    ]
                ]
            }
        ]
    }
    
]