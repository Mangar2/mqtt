module.exports = [
    {
        description: 'Test cases for missing acknowledgements',
        tests: [
            {
                description: 'Connect and subscribe to topics with QoS 0, 1, and 2',
                connect: [
                    {
                        clientId: 'clientA', host: 'hostA', port: 'portA', clean: true, version: '1.0', keepAlive: 100
                    }
                ],
                subscribe: [
                    { client: 'clientA', topic: { 'topic/qos0': 0, 'topic/qos1': 1, 'topic/qos2': 2 } }
                ],
                expected: []
            },
            {
                description: 'Publish messages with QoS 0, 1, and 2 without acknowledgement',
                acknowledge: false,
                publish: [
                    { topic: 'topic/qos0', value: 'valueQos0', qos: 0, retain: false },
                    { topic: 'topic/qos1', value: 'valueQos1', qos: 1, retain: false },
                    { topic: 'topic/qos2', value: 'valueQos2', qos: 2, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topic/qos0', value: 'valueQos0', qos: 0 }, clientId: 'clientA', status: 'new' },
                        { message: { topic: 'topic/qos1', value: 'valueQos1', qos: 1 }, clientId: 'clientA', status: 'sending' },
                        { message: { topic: 'topic/qos2', value: 'valueQos2', qos: 2 }, clientId: 'clientA', status: 'sending' }
                    ]
                ]
            },
            {
                description: 'Delay less than 10 milliseconds',
                delay: 5,
                expected: []
            },
            {
                description: 'Check if the QoS 1 and 2 messages are still not received',
                delay: 10,
                expected: [
                    [
                        { message: { topic: 'topic/qos1', value: 'valueQos1', qos: 1 }, clientId: 'clientA', status: 'duplicate' },
                        { message: { topic: 'topic/qos2', value: 'valueQos2', qos: 2 }, clientId: 'clientA', status: 'duplicate' }
                    ],
                    [
                        { message: { topic: 'topic/qos2', value: 'valueQos2', qos: 2 }, clientId: 'clientA', status: 'pubrel' }
                    ]
                ]
            }
        ]
    }
    
]