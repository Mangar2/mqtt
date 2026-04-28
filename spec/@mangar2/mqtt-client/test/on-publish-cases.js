module.exports =
[
    {
        description: 'Test publishing messages with QoS 0',
        configuration: { retry: 1 },
        tests: [
            {
                description: 'Publish a successful message version 0.0',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic1',
                    value: 'Test Message QoS 0',
                    qos: 0
                },
                version: '0.0',
                expected: {
                    result: [['send (qos 0)']],
                    'history': [
                        {
                            message: {
                                'topic': 'test/topic1',
                                'value': 'Test Message QoS 0',
                                'qos': 0,
                                'retain': false
                            },
                            dup: false
                        }
                    ]
                }
            },
            {
                description: 'Publish a successful message version 1.0',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic1',
                    value: 'Test Message QoS 0',
                    qos: 0
                },
                version: '1.0',
                expected: {
                    result: [['send (qos 0)']],
                    'history': [
                        {
                            message: {
                                'topic': 'test/topic1',
                                'value': 'Test Message QoS 0',
                                'qos': 0,
                                'retain': false
                            },
                            dup: false
                        }
                    ]
                }
            }
        ]
    },
    {
        description: 'Test publishing messages with QoS 1',
        configuration: { retry: 2 },
        tests: [
            {
                description: 'Correct single message',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic1',
                    value: 'Test Message QoS 1',
                    qos: 1
                },
                version: '1.0',
                expected: {
                    result: [['delivered (qos 1)']],
                    'history': [
                        {
                            message: {
                                'topic': 'test/topic1',
                                'value': 'Test Message QoS 1',
                                'qos': 1,
                                'retain': false
                            },
                            dup: false
                        }
                    ]
                }
            }
        ]
    },
    {
        description: 'Test publishing messages with QoS 2',
        configuration: { retry: 2 },
        tests: [
            {
                description: 'Correct single message',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic1',
                    value: 'Test Message QoS 2',
                    qos: 2
                },
                version: '1.0',
                expected: {
                    result: [['delivered (qos 2)']],
                    'history': [
                        {
                            message: {
                                'topic': 'test/topic1',
                                'value': 'Test Message QoS 2',
                                'qos': 2,
                                'retain': false
                            },
                            dup: false
                        }
                    ]
                }
            }
        ]
    }
]
