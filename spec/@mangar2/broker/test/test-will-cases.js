module.exports = [
    {
        description: 'Test cases for MQTT Last Will and Testament feature',
        connect: [
            {
                clientId: 'clientWill1', host: 'hostW1', port: 'portW1', clean: false, version: '1.0', keepAlive: 100,
                will: { topic: 'willTopic1', value: 'willMessage1', qos: 1 }
            },
            {
                clientId: 'clientWill2', host: 'hostW2', port: 'portW2', clean: false, version: '1.0', keepAlive: 1000,
                will: { topic: 'willTopic2', value: 'willMessage2', qos: 2 }
            }
        ],
        subscribe: [
            { client: 'clientWill1', topic: { 'willTopic2': 2, 'testTopic': 1 } },
            { client: 'clientWill2', topic: { 'willTopic1': 1, 'testTopic': 1 } }
        ],
        tests: [
            {
                description: 'Delay exceeding keep-alive for clientWill1, inducing automatic disconnect and triggering the Will message',
                delay: 200,
                expected: [
                    [
                        { message: { topic: 'willTopic1', value: 'willMessage1', qos: 1 }, clientId: 'clientWill2', status: 'sending' }
                    ]
                ]
            },
            {
                description: 'Reconnect clientWill1',
                connect: [
                    { clientId: 'clientWill1', host: 'hostW1', port: 'portW1', clean: false, version: '1.0', keepAlive: 100 }
                ],
                expected: []
            },
            {
                description: 'Verify clientWill1 does not receive Will message upon manual reconnection, but clientWill1, clientWill2 is still subscribed',
                publish: [
                    { topic: 'testTopic', value: 'normalMessage', qos: 1, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'testTopic', value: 'normalMessage', qos: 1 }, clientId: 'clientWill1', status: 'sending' },
                        { message: { topic: 'testTopic', value: 'normalMessage', qos: 1 }, clientId: 'clientWill2', status: 'sending' }
                    ]
                ]
            }
        ]
    }
]