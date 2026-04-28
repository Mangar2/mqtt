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
                onPublish: {
                    statusCode: 204
                },
                expected: {
                    result: ['send (qos 0)'],
                    'history': [
                        {
                            path: '/publish',
                            method: 'put',
                            headers: {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '0',
                                'version': '0.0'
                            },
                            payload: {
                                'topic': 'test/topic1',
                                'value': 'Test Message QoS 0',
                                'qos': 0,
                                'retain': false
                            }
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
                onPublish: {
                    statusCode: 204
                },
                expected: {
                    result: ['send (qos 0)'],
                    'history': [
                        {
                            path: '/publish',
                            method: 'put',
                            headers: {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '0',
                                'dup': '0',
                                'retain': '0',
                                'version': '1.0'
                            },
                            payload: {
                                'token': 'testToken123',
                                'message': {
                                    'topic': 'test/topic1',
                                    'value': 'Test Message QoS 0'
                                }
                            }
                        }
                    ]
                }
            },
            {
                description: 'Publish function with missing value in the message',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic2',
                    value: undefined, 
                    qos: 0
                },
                version: '1.0',
                onPublish: {
                    statusCode: 204 
                },
                expected: {
                    result: ['send (qos 0)'],
                    'history': [
                        {
                            payload: {
                                'message': {
                                    'value': ''
                                }
                            }
                        }
                    ]
                }
            },
            {
                description: 'Publish function handling bad status code',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic2',
                    value: undefined, // Value that causes serialization to fail
                    qos: 0
                },
                version: '1.0',
                onPublish: {
                    statusCode: 404 // Server would still respond OK
                },
                expected: {
                    'result': ['not send (qos 0)']
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
                onPublish: {
                    statusCode: 204,
                    headers: { packetid: '1', packet: 'puback' }
                },
                expected: {
                    result: ['delivered (qos 1)'],
                    'history': [
                        {
                            path: '/publish',
                            method: 'put',
                            headers: {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '1',
                                'dup': '0',
                                'retain': '0',
                                'version': '1.0',
                                'packetid': '1'
                            },
                            payload: {
                                'token': 'testToken123',
                                'message': {
                                    'topic': 'test/topic1',
                                    'value': 'Test Message QoS 1'
                                }
                            }
                        }
                    ]
                }
            },
            {
                description: 'Publish message without receiving PUBACK',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic2',
                    value: 'Test Message QoS 1',
                    qos: 1
                },
                version: '1.0',
                onPublish: {
                    statusCode: 204,
                    headers: { packetid: '2', packet: '' }
                },
                expected: {
                    'result': ['Error publishing message: acknowledge \'puback\' expected, got '],
                    'history': [
                        {
                            'path': '/publish',
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '1',
                                'dup': '0',
                                'retain': '0',
                                'version': '1.0',
                                'packetid': '2'
                            },
                            'payload': {
                                'token': 'testToken123',
                                'message': {
                                    'topic': 'test/topic2',
                                    'value': 'Test Message QoS 1'
                                }
                            }
                        },
                        {
                            'path': '/publish',
                            'method': 'put',
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '1',
                                'dup': '1',
                                'retain': '0',
                                'version': '1.0',
                                'packetid': '2'
                            },
                            'payload': {
                                'token': 'testToken123',
                                'message': {
                                    'topic': 'test/topic2',
                                    'value': 'Test Message QoS 1'
                                }
                            }
                        }
                    ]
                }
            },
            {
                description: 'Publish message with incorrect PUBACK packetid',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic3',
                    value: 'Test Message QoS 1',
                    qos: 1
                },
                version: '1.0',
                onPublish: {
                    statusCode: 204,
                    headers: { packetid: '4', packet: 'puback' } 
                },
                expected: {
                    result: ['Error publishing message: packet id 3 expected, got 4'],
                    history: [
                        {
                            path: '/publish',
                            method: 'put',
                            headers: {
                                'qos': '1',
                                'dup': '0',
                                'packetid': '3'
                            }
                        },
                        {
                            path: '/publish',
                            method: 'put',
                            headers: {
                                'qos': '1',
                                'dup': '1',
                                'packetid': '3'
                            }
                        }
                    ]
                }
            },
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
                onPublish: {
                    statusCode: 204,
                    headers: { packetid: '1', packet: 'pubrec' }
                },
                onPubrel: {
                    statusCode: 204,
                    headers: { packetid: '1', packet: 'pubcomp' }
                },
                expected: {
                    result: ['delivered (qos 2)'],
                    'history': [
                        {
                            path: '/publish',
                            method: 'put',
                            headers: {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '2',
                                'dup': '0',
                                'retain': '0',
                                'version': '1.0',
                                'packetid': '1'
                            },
                            payload: {
                                'token': 'testToken123',
                                'message': {
                                    'topic': 'test/topic1',
                                    'value': 'Test Message QoS 2'
                                }
                            }
                        },
                        {
                            path: '/pubrel',
                            method: 'put',
                            headers: {
                                'content-type': 'text/plain; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'version': '1.0',
                                'packetid': '1'
                            },
                            payload: {
                                token: 'testToken123'
                            }
                        },
                    ]
                }
            },
            {
                description: 'missing pubcomp',
                method: 'publish',
                token: 'testToken123',
                message: {
                    topic: 'test/topic1',
                    value: 'Test Message QoS 2',
                    qos: 2
                },
                version: '1.0',
                onPublish: {
                    statusCode: 204,
                    headers: { packetid: '2', packet: 'pubrec' }
                },
                onPubrel: {
                    statusCode: 204,
                    headers: { packetid: '2', packet: 'pubrec' }
                },
                expected: {
                    result: ['Error publishing message: acknowledge \'pubcomp\' expected, got pubrec'],
                    'history': [
                        {
                            path: '/publish',
                            headers: {
                                'qos': '2',
                                'dup': '0',
                                'packetid': '2'
                            }
                        },
                        {
                            path: '/pubrel',
                            headers: {
                                'version': '1.0',
                                'packetid': '2'
                            },
                            payload: {
                                token: 'testToken123'
                            }
                        },
                        {
                            path: '/pubrel',
                            headers: {
                                'version': '1.0',
                                'packetid': '2'
                            },
                            payload: {
                                token: 'testToken123'
                            }
                        }
                    ]
                }
            },
        ]
    }
]
