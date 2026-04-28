module.exports = [
    {
        description: 'Test case with unsubscribe operations',
        connect: [
            {
                clientId: 'clientU1', host: 'hostU1', port: 'portU1', clean: false, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'clientU2', host: 'hostU2', port: 'portU2', clean: false, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'clientU1', topic: { 'topicUnsub1': 1, 'topicUnsub2': 2 } },
            { client: 'clientU2', topic: { 'topicUnsub1': 2 } }
        ],
        tests: [
            {
                description: 'Publish to topicUnsub1 and topicUnsub2',
                publish: [
                    { topic: 'topicUnsub1', value: 'value1', qos: 1, retain: false },
                    { topic: 'topicUnsub2', value: 'value2', qos: 2, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topicUnsub1', value: 'value1', qos: 1 }, clientId: 'clientU1', status: 'sending' },
                        { message: { topic: 'topicUnsub2', value: 'value2', qos: 2 }, clientId: 'clientU1', status: 'sending' },
                        { message: { topic: 'topicUnsub1', value: 'value1', qos: 1 }, clientId: 'clientU2', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topicUnsub2', value: 'value2', qos: 2 }, clientId: 'clientU1', status: 'pubrel' }
                    ]
                ]
            },
            {
                description: 'Unsubscribe clientU1 from topicUnsub1',
                unsubscribe: [{ clientId: 'clientU1', topics: ['topicUnsub1'] }],
                expected: []
            },
            {
                description: 'Publish to topicUnsub1 after clientU1 unsubscribed',
                publish: [{ topic: 'topicUnsub1', value: 'valueAfterUnsub1', qos: 1, retain: false }],
                expected: [
                    [
                        { message: { topic: 'topicUnsub1', value: 'valueAfterUnsub1', qos: 1 }, clientId: 'clientU2', status: 'sending' }
                    ]
                ]
            },
            {
                description: 'Unsubscribe clientU2 from topicUnsub1',
                unsubscribe: [{ clientId: 'clientU2', topics: ['topicUnsub1'] }],
                expected: []
            },
            {
                description: 'Publish to topicUnsub1 after both clients unsubscribed',
                publish: [{ topic: 'topicUnsub1', value: 'valueAfterBothUnsub', qos: 1, retain: false }],
                expected: []
            }
        ]
    },
    {
        description: 'Test case with multiple unsubscribes in one operation',
        connect: [
            {
                clientId: 'clientUM', host: 'hostUM', port: 'portUM', clean: false, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'clientUM', topic: { 'topicMultiUnsub1': 1, 'topicMultiUnsub2': 2, 'topicMultiUnsub3': 2 } }
        ],
        tests: [
            {
                description: 'Publish to topicMultiUnsub1, topicMultiUnsub2, and topicMultiUnsub3',
                publish: [
                    { topic: 'topicMultiUnsub1', value: 'valueMU1', qos: 1, retain: false },
                    { topic: 'topicMultiUnsub2', value: 'valueMU2', qos: 2, retain: false },
                    { topic: 'topicMultiUnsub3', value: 'valueMU3', qos: 2, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topicMultiUnsub1', value: 'valueMU1', qos: 1 }, clientId: 'clientUM', status: 'sending' },
                        { message: { topic: 'topicMultiUnsub2', value: 'valueMU2', qos: 2 }, clientId: 'clientUM', status: 'sending' },
                        { message: { topic: 'topicMultiUnsub3', value: 'valueMU3', qos: 2 }, clientId: 'clientUM', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topicMultiUnsub2', value: 'valueMU2', qos: 2 }, clientId: 'clientUM', status: 'pubrel' },
                        { message: { topic: 'topicMultiUnsub3', value: 'valueMU3', qos: 2 }, clientId: 'clientUM', status: 'pubrel' }
                    ]
                ]
            },
            {
                description: 'Unsubscribe clientUM from topicMultiUnsub1 and topicMultiUnsub2',
                unsubscribe: [{ clientId: 'clientUM', topics: ['topicMultiUnsub1', 'topicMultiUnsub2'] }],
                expected: []
            },
            {
                description: 'Publish to topicMultiUnsub1, topicMultiUnsub2, and topicMultiUnsub3 after unsubscribing from topicMultiUnsub1 and topicMultiUnsub2',
                publish: [
                    { topic: 'topicMultiUnsub1', value: 'valueAfterUnsubMU1', qos: 1, retain: false },
                    { topic: 'topicMultiUnsub2', value: 'valueAfterUnsubMU2', qos: 2, retain: false },
                    { topic: 'topicMultiUnsub3', value: 'valueAfterUnsubMU3', qos: 2, retain: false }
                ],
                expected: [
                    [
                        { message: { topic: 'topicMultiUnsub3', value: 'valueAfterUnsubMU3', qos: 2 }, clientId: 'clientUM', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topicMultiUnsub3', value: 'valueAfterUnsubMU3', qos: 2 }, clientId: 'clientUM', status: 'pubrel' }
                    ]
                ]
            }
        ]
    }
]