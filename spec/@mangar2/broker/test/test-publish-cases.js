module.exports = [
    {
        description: 'Connect',
        connect: [
            {
                clientId: 'client0', host: 'host0', port: 'port0', clean: false, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'client1', host: 'host1', port: 'port1', clean: true, version: '1.0', keepAlive: 100
            }
        ],
        tests: [
            {
                description: 'Connection does not result in messages to send',
                expected: []
            }
        ]
    },
    {
        description: 'Subscription with qos: 0',
        connect: [
            {
                clientId: 'client0', host: 'host0', port: 'port0', clean: false, version: '1.0', keepAlive: 100
            }
        ],        
        subscribe: [
            { client: 'client0', topic: { 'topic/0': 0 } }
        ],
        tests: [
            {
                description: 'Subscriptions does not lead to messages to send',
                expected: []
            },
            {
                description: 'publish to topic/0',
                publish: [{ topic: 'topic/0', value: 'value0', reason: 'reason0', qos: 0, retain: false }],
                expected: [[
                    {
                        'message': {
                            'topic': 'topic/0',
                            'value': 'value0',
                            'qos': 0,
                            'retain': false,
                            'reason': [
                                {
                                    'message': 'reason0',
                                }
                            ]
                        },
                        'transmitTimestamp': 0,
                        'status': 'new',
                        'retryCount': 0,
                        'clientId': 'client0',
                        'host': 'host0',
                        'port': 'port0',
                        'version': '1.0',
                        'token': 'receiveclient0'
                    }
                ]]
            },
            {
                description: 'Messages are only availabe once, the queue is now empty',
                expected: []
            },
            {
                description: 'sending message having no subscription',
                publish: [{ topic: 'topic/1', value: 'value1', reason: 'reason0', qos: 0, retain: false }],
                expected: []
            },
            {
                description: 'sending two messages with qos0',
                publish: [
                    { topic: 'topic/0', value: 'value1', reason: 'reason0', qos: 0, retain: false },
                    { topic: 'topic/0', value: 'value2', reason: 'reason0', qos: 0, retain: false }
                ],
                expected: [[
                    {'message': { 'value': 'value1'}}, {'message': { 'value': 'value2'}}
                ]]
            },
            {
                description: 'sending one messages with qos 1, it must still have qos 0, as we subscribed with qos 0',
                publish: [
                    { topic: 'topic/0', value: 'value1', reason: 'reason0', qos: 1, retain: false }
                ],
                expected: [[
                    {'message': { 'value': 'value1', 'qos': 0}}
                ]]
            },
            {
                description: 'sending one messages with qos 2, it must still have qos 0, as we subscribed with qos 0',
                publish: [
                    { topic: 'topic/0', value: 'value2', reason: 'reason0', qos: 2, retain: false }
                ],
                expected: [[
                    {'message': { 'value': 'value2', 'qos': 0}}
                ]]
            }
        ]
    },
    {
        description: 'Subscription with qos: 1',
        connect: [
            {
                clientId: 'client1', host: 'host1', port: 'port1', clean: false, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'client1', topic: { 'topic/1': 1 } }
        ],
        tests: [
            {
                description: 'publish to topic/1',
                publish: [{ topic: 'topic/1', value: 'value3', qos: 1, retain: false }],
                expected: [[
                    {
                        message: {
                            topic: 'topic/1',
                            value: 'value3',
                            qos: 1
                        },
                        status: 'sending'
                    }
                ]]
            },
            {
                description: 'publish to topic/1 again, inflight window is 1 so only first message is delivered',
                publish: [
                    { topic: 'topic/1', value: 'value4', qos: 1, retain: false },
                    { topic: 'topic/1', value: 'value5', qos: 1, retain: false },
                ],
                expected: [
                    [
                        {
                            message: {
                                value: 'value4'
                            }
                        }
                    ],
                    [
                        {
                            message: {
                                value: 'value5'
                            }
                        }
                    ]
                ]
            },
            {
                description: 'sending two messages with qos1, only first message is delivered due to inflight window',
                publish: [
                    { topic: 'topic/1', value: 'sameValue', qos: 1, retain: false },
                    { topic: 'topic/1', value: 'sameValue', qos: 1, retain: false }
                ],
                expected: [
                    [{message: { value: 'sameValue'}}],[{message: { value: 'sameValue'}}]
                ]
            },
            {
                description: 'sending one message with qos 2, it must still have qos 1, as we subscribed with qos 1',
                publish: [
                    { topic: 'topic/1', value: 'value8', qos: 2, retain: false }
                ],
                expected: [
                    [{message: { value: 'value8', qos: 1}}]
                ]
            },
            {
                description: 'sending a qos0 message, it should be delivered with qos 0 even though we subscribed with qos 1',
                publish: [
                    { topic: 'topic/1', value: 'value0', qos: 0, retain: false }
                ],
                expected: [
                    [{ message: { value: 'value0', qos: 0 } }]
                ]
            },
            {
                description: 'sending qos1 message with retain flag',
                publish: [
                    { topic: 'topic/1', value: 'valueRetain', qos: 1, retain: true }
                ],
                expected: [
                    [{ message: { value: 'valueRetain', retain: true } }]
                ]
            },
            {
                description: 'no new publish, test case to confirm no more messages are delivered',
                expected: []
            }
        ]
    },
    {
        description: 'Subscription with qos: 2',
        connect: [
            {
                clientId: 'client2', host: 'host2', port: 'port2', clean: false, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'client2', topic: { 'topic/2': 2 } }
        ],
        tests: [
            {
                description: 'Subscriptions does not lead to messages to send',
                expected: []
            },
            {
                description: 'publish to topic/2 with qos 2, message status should be "sending" followed by "pubrel"',
                publish: [{ topic: 'topic/2', value: 'valueQos2', qos: 2, retain: false }],
                expected: [
                    [
                        {
                            message: {
                                topic: 'topic/2',
                                value: 'valueQos2',
                                qos: 2
                            },
                            status: 'sending'
                        }
                    ],
                    [
                        {
                            message: {
                                topic: 'topic/2',
                                value: 'valueQos2',
                                qos: 2
                            },
                            status: 'pubrel'
                        }
                    ]
                ]
            },
            {
                description: 'no new publish, message should now be gone after the four-step handshake',
                expected: []
            },
            {
                description: 'sending qos0 message, it should be delivered with qos 0 even though we subscribed with qos 2',
                publish: [
                    { topic: 'topic/2', value: 'valueQos0', qos: 0, retain: false }
                ],
                expected: [
                    [
                        { message: { value: 'valueQos0', qos: 0 } }
                    ]
                ]
            },
            {
                description: 'sending qos1 message, it should be delivered with qos 1 even though we subscribed with qos 2',
                publish: [
                    { topic: 'topic/2', value: 'valueQos1', qos: 1, retain: false }
                ],
                expected: [
                    [
                        { message: { value: 'valueQos1', qos: 1 } }
                    ]
                ]
            },
            {
                description: 'sending two qos2 messages with the same value, one message is delivered after the other to inflight window',
                publish: [
                    { topic: 'topic/2', value: 'valueSame', qos: 2, retain: false },
                    { topic: 'topic/2', value: 'valueSame', qos: 2, retain: false }
                ],
                expected: [
                    [
                        { message: { value: 'valueSame' }, status: 'sending' }
                    ],
                    [
                        { message: { value: 'valueSame' }, status: 'pubrel' }
                    ],
                    [
                        { message: { value: 'valueSame' }, status: 'sending' }
                    ],
                    [
                        { message: { value: 'valueSame' }, status: 'pubrel' }
                    ]
                ]
            }
        ]
    },
    {
        description: 'Multiple subscriptions with different qos levels',
        connect: [
            {
                clientId: 'clientMulti', host: 'hostMulti', port: 'portMulti', clean: false, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'clientMulti', topic: { 'topic/0': 0, 'topic/1': 1, 'topic/2': 2 } }
        ],
        tests: [
            {
                description: 'No initial messages to send',
                expected: []
            },
            {
                description: 'publish to topic/0 with qos 0',
                publish: [{ topic: 'topic/0', value: 'value0', qos: 0, retain: false }],
                expected: [
                    [
                        { message: { topic: 'topic/0', value: 'value0', qos: 0 } }
                    ]
                ]
            },
            {
                description: 'publish to topic/1 with qos 1, message status should be "sending"',
                publish: [{ topic: 'topic/1', value: 'value1', qos: 1, retain: false }],
                expected: [
                    [
                        { message: { topic: 'topic/1', value: 'value1', qos: 1 }, status: 'sending' }
                    ]
                ]
            },
            {
                description: 'publish to topic/2 with qos 2, message status should be "sending" followed by "pubrel"',
                publish: [{ topic: 'topic/2', value: 'value2', qos: 2, retain: false }],
                expected: [
                    [
                        { message: { topic: 'topic/2', value: 'value2', qos: 2 }, status: 'sending' }
                    ],
                    [
                        { message: { topic: 'topic/2', value: 'value2', qos: 2 }, status: 'pubrel' }
                    ]
                ]
            },
            {
                description: 'no new publish, message should now be gone after the four-step handshake',
                expected: []
            },
            {
                description: 'sending a message to an unsubscribed topic should result in no message',
                publish: [{ topic: 'topic/unsubscribed', value: 'valueUnsubscribed', qos: 1, retain: false }],
                expected: []
            }
        ]
    },
    {
        description: 'Three clients subscribing to the same topic with different qos levels',
        connect: [
            {
                clientId: 'clientA', host: 'hostA', port: 'portA', clean: false, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'clientB', host: 'hostB', port: 'portB', clean: false, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'clientC', host: 'hostC', port: 'portC', clean: false, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'clientA', topic: { 'sharedTopic': 0 } },
            { client: 'clientB', topic: { 'sharedTopic': 1 } },
            { client: 'clientC', topic: { 'sharedTopic': 2 } }
        ],
        tests: [
            {
                description: 'No initial messages to send',
                expected: []
            },
            {
                description: 'Publish to sharedTopic with qos 0',
                publish: [{ topic: 'sharedTopic', value: 'value0', qos: 0, retain: false }],
                expected: [
                    [
                        { message: { topic: 'sharedTopic', value: 'value0', qos: 0 }, clientId: 'clientA' },
                        { message: { topic: 'sharedTopic', value: 'value0', qos: 0 }, clientId: 'clientB', status: 'new' },
                        { message: { topic: 'sharedTopic', value: 'value0', qos: 0 }, clientId: 'clientC', status: 'new' }
                    ]
                ]
            },
            {
                description: 'Publish to sharedTopic with qos 2',
                publish: [{ topic: 'sharedTopic', value: 'value2', qos: 2, retain: false }],
                expected: [
                    [
                        { message: { topic: 'sharedTopic', value: 'value2', qos: 0 }, clientId: 'clientA' },
                        { message: { topic: 'sharedTopic', value: 'value2', qos: 1 }, clientId: 'clientB', status: 'sending' },
                        { message: { topic: 'sharedTopic', value: 'value2', qos: 2 }, clientId: 'clientC', status: 'sending' }
                    ],
                    [
                        { message: { topic: 'sharedTopic', value: 'value2', qos: 2 }, clientId: 'clientC', status: 'pubrel' }
                    ]
                ]
            }
        ]
    }
]
