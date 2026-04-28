module.exports = [
    {
        description: 'Test case with disconnect operations',
        connect: [
            {
                clientId: 'clientD', host: 'hostD', port: 'portD', clean: false, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'clientE', host: 'hostE', port: 'portE', clean: false, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'clientF', host: 'hostF', port: 'portF', clean: false, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'clientD', topic: { 'topicDisconnect': 0 } },
            { client: 'clientE', topic: { 'topicDisconnect': 1 } },
            { client: 'clientF', topic: { 'topicDisconnect': 2 } }
        ],
        tests: [
            {
                description: 'Publish to topicDisconnect with qos 2',
                publish: [{ topic: 'topicDisconnect', value: 'valueBeforeDisconnect', qos: 2, retain: false }],
                expected: [
                    [
                        { message: { topic: 'topicDisconnect', value: 'valueBeforeDisconnect', qos: 0 }, clientId: 'clientD' },
                        { message: { topic: 'topicDisconnect', value: 'valueBeforeDisconnect', qos: 1 }, clientId: 'clientE', status: 'sending' },
                        { message: { topic: 'topicDisconnect', value: 'valueBeforeDisconnect', qos: 2 }, clientId: 'clientF', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topicDisconnect', value: 'valueBeforeDisconnect', qos: 2 }, clientId: 'clientF', status: 'pubrel' }
                    ]
                ]
            },
            {
                description: 'Disconnect clientE',
                disconnect: ['clientE'],
                expected: []
            },
            {
                description: 'Publish to topicDisconnect after clientE disconnect',
                publish: [{ topic: 'topicDisconnect', value: 'valueAfterDisconnect', qos: 2, retain: false }],
                expected: [
                    [
                        { message: { topic: 'topicDisconnect', value: 'valueAfterDisconnect', qos: 0 }, clientId: 'clientD' },
                        { message: { topic: 'topicDisconnect', value: 'valueAfterDisconnect', qos: 2 }, clientId: 'clientF', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topicDisconnect', value: 'valueAfterDisconnect', qos: 2 }, clientId: 'clientF', status: 'pubrel' }
                    ]
                ]
            },
            {
                description: 'Disconnect clientD and clientF',
                disconnect: ['clientD', 'clientF'],
                expected: []
            },
            {
                description: 'Publish to topicDisconnect after all clients disconnect',
                publish: [{ topic: 'topicDisconnect', value: 'valueAfterAllDisconnect', qos: 2, retain: false }],
                expected: []
            }
        ]
    },
    {
        description: 'Test case for receiving messages sent while the client was disconnected',
        tests: [
            {
                description: 'Connect and subscribe to a topic, then disconnect the client',
                connect: [
                    {
                        clientId: 'clientDisconnected', host: 'hostDisconnected', port: 'portDisconnected', clean: false, version: '1.0', keepAlive: 100
                    }
                ],
                subscribe: [
                    { client: 'clientDisconnected', topic: { 'topic/disconnected': 1 } }
                ],
                disconnect: ['clientDisconnected'],
                expected: []
            },
            {
                description: 'Publish messages while the client is disconnected',
                publish: [
                    { topic: 'topic/disconnected', value: 'valueDisconnected1', qos: 1, retain: false },
                    { topic: 'topic/disconnected', value: 'valueDisconnected2', qos: 1, retain: false }
                ],
                expected: []
            },
            {
                description: 'Reconnect the client and check that the messages sent while disconnected are received',
                connect: [
                    {
                        clientId: 'clientDisconnected', host: 'hostDisconnected', port: 'portDisconnected', clean: false, version: '1.0', keepAlive: 100
                    }
                ],
                expected: [
                    [
                        { message: { topic: 'topic/disconnected', value: 'valueDisconnected1', qos: 1 }, clientId: 'clientDisconnected', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topic/disconnected', value: 'valueDisconnected2', qos: 1 }, clientId: 'clientDisconnected', status: 'sending' }
                    ]
                ]
            }
        ]
    }
    
]