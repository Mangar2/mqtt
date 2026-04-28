module.exports = [
    {
        description: 'Connect test',
        clientId: 'test-client',
        tests: [
            {
                description: 'Testing connect method, clean session',
                method: 'connect',
                args: [{ version: '1.0', clean: true, keepAlive: 600000 }],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'connack' }, 
                    payload: '{ "present": 0, "mqttcode": 0, "token": {"send": "abcd", "receive": "efgh" }}' 
                },
                expected: {
                    'result': {
                        present: 0,
                        mqttcode: 0,
                        token: { send: 'abcd', receive: 'efgh' }
                    },
                    'history': [
                        {
                            'path': 'put',
                            'method': '/connect',
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'version': '1.0'
                            },
                            'payload': {
                                'clientId': 'test-client',
                                'host': 'localhost',
                                'port': 0,
                                'clean': true,
                                'keepAlive': 600000
                            }
                        }
                    ]
                }
            },
            {
                description: 'Testing connect method, non clean session',
                method: 'connect',
                args: [{ version: '1.0', clean: false, keepAlive: 600000 }],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'connack' }, 
                    payload: '{ "present": 1, "mqttcode": 0, "token": {"send": "abcd", "receive": "efgh" }}'
                },
                expected: {
                    'result': { present: 1 },
                    'history': [
                        {
                            'payload': {
                                'clean': false,
                            }
                        }
                    ]
                }
            },
            {
                description: 'Testing connect method, missing connack',
                method: 'connect',
                args: [{ version: '1.0', clean: false, keepAlive: 600000 }],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'ack' }, 
                    payload: '{ "present": 1, "mqttcode": 0, "token": {"send": "abcd", "receive": "efgh" }}'
                },                
                expected: {
                    'result': 'Unable to connect: acknowledge \'connack\' expected, got ack',
                    'history': [
                        {
                            'payload': {
                                'clean': false,
                            }
                        }
                    ]
                }
            },
            {
                description: 'Testing connect method, missing token',
                method: 'connect',
                args: [{ version: '1.0', clean: false, keepAlive: 600000 }],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'connack' }, 
                    payload: '{ "present": 1, "mqttcode": 0 }'
                },                  
                expected: {
                    'result': 'Unable to connect: send/receive token not completely received',
                    'history': [
                        {
                            'payload': {
                                'clean': false,
                            }
                        }
                    ]
                }
            },
            {
                description: 'Testing connect method, headers not application/json',
                method: 'connect',
                args: [{ version: '1.0', clean: false, keepAlive: 600000 }],
                result: { statusCode: 200, headers: { 'content-type': 'text', packet: 'connack' }, payload: '{ }' },
                expected: {
                    'result': 'Unable to connect: content-type is not application/json',
                    'history': [
                        {
                            'payload': {
                                'clean': false,
                            }
                        }
                    ]
                }
            }
        ]
    },
    {
        description: 'Disconnect tests',
        clientId: 'test-client',
        tests: [
            {
                description: 'Testing disconnect method, success',
                method: 'disconnect',
                args: ['1.0'],
                result: { statusCode: 204, headers: { 'content-type': 'text' }},
                expected: {
                    'history': [
                        {
                            'path': 'put',
                            'method': '/disconnect',
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'version': '1.0'
                            },
                            'payload': {
                                'clientId': 'test-client'
                            }
                        }
                    ]
                }
            },
            {
                description: 'Testing disconnect method, not success',
                method: 'disconnect',
                args: ['1.0'],
                result: { statusCode: 400, headers: { 'content-type': 'text' }},
                expected: {
                    result: 'Unable to disconnect: status code 204 expected, got 400',
                    'history': [
                        {
                            'path': 'put',
                            'method': '/disconnect',
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'version': '1.0'
                            },
                            'payload': {
                                'clientId': 'test-client'
                            }
                        }
                    ]
                }
            },
        ]
    }
]
