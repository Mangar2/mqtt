module.exports = [
    {
        description: 'Test cases for MQTT with InFlightWindow set to 2',
        inFlightWindow: 2,
        connect: [
            {
                clientId: 'clientIFW', host: 'hostIFW', port: 'portIFW', clean: true, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'clientIFW', topic: { 'topic/ifw': 1 } }
        ],
        tests: [
            {
                description: 'Publish two messages to topic/ifw and expect clientIFW to receive them simultaneously',
                publish: [
                    { topic: 'topic/ifw', value: 'valueIFW1', qos: 1, retain: false },
                    { topic: 'topic/ifw', value: 'valueIFW2', qos: 1, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topic/ifw', value: 'valueIFW1', qos: 1 }, clientId: 'clientIFW', status: 'sending' },
                        { message: { topic: 'topic/ifw', value: 'valueIFW2', qos: 1 }, clientId: 'clientIFW', status: 'sending' }
                    ]
                ]
            },
            {
                description: 'Publish three messages to topic/ifw and expect clientIFW to receive two simultaneously and the third afterward',
                publish: [
                    { topic: 'topic/ifw', value: 'valueIFW3', qos: 1, retain: false },
                    { topic: 'topic/ifw', value: 'valueIFW4', qos: 1, retain: false },
                    { topic: 'topic/ifw', value: 'valueIFW5', qos: 1, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topic/ifw', value: 'valueIFW3', qos: 1 }, clientId: 'clientIFW', status: 'sending' },
                        { message: { topic: 'topic/ifw', value: 'valueIFW4', qos: 1 }, clientId: 'clientIFW', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topic/ifw', value: 'valueIFW5', qos: 1 }, clientId: 'clientIFW', status: 'sending' }
                    ]
                ]
            }
        ]
    }
    
]