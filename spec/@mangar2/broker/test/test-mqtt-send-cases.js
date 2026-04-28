module.exports = [
    {
        description: '',
        configuration: {
            fileName: 'hello',
            directory: '.'
        },
        connect: [
            {
                clientId: 'client0', host: 'host0', port: 'port0', clean: false, version: '1.0', keepAlive: 100
            },
            {
                clientId: 'client1', host: 'host1', port: 'port1', clean: true, version: '1.0', keepAlive: 100
            }
        ],
        subscribe: [
            { client: 'client0', topic: { 'topic/0': 0 } }
        ],
        tests: [
            {
                description: 'publishing messages',
                publish: [
                    { topic: 'topic/0', value: 'value1', reason: 'reason0', qos: 0, retain: false },
                    { topic: 'topic/0', value: 'value2', reason: 'reason0', qos: 0, retain: false }
                ],
                expected:
                    [
                        {
                            'host': 'host0',
                            'port': 'port0',
                            'command': 'publish',
                            'payload': {
                                'token': 'receiveclient0',
                                'message': {
                                    'topic': 'topic/0',
                                    'value': 'value1',
                                    'reason': [
                                        {
                                            'message': 'reason0'
                                        }
                                    ]
                                }
                            },
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '0',
                                'dup': 'false',
                                'retain': 'false',
                                'version': '1.0',
                                'packetid': '0'
                            }
                        },
                        {
                            'host': 'host0',
                            'port': 'port0',
                            'command': 'publish',
                            'payload': {
                                'token': 'receiveclient0',
                                'message': {
                                    'topic': 'topic/0',
                                    'value': 'value2',
                                    'reason': [
                                        {
                                            'message': 'reason0'
                                        }
                                    ]
                                }
                            },
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'qos': '0',
                                'dup': 'false',
                                'retain': 'false',
                                'version': '1.0',
                                'packetid': '0'
                            }
                        }
                    ]
            },
        ]
    }
]