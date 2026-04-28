module.exports = [
    {
        description: 'Subscribe test',
        clientId: 'test-client',
        tests: [
            {
                description: 'Testing subscribe, suback missing',
                method: 'subscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'connack' }, 
                },
                expected: {
                    'result': 'Unable to subscribe: acknowledge \'suback\' expected, got connack'
                }
            },
            {
                description: 'Testing subscribe, packetid missing',
                method: 'subscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'suback' }, 
                },
                expected: {
                    'result': 'Unable to subscribe: wrong packet id expected: 2, got undefined'
                }
            },
            {
                description: 'Testing subscribe, payload missing',
                method: 'subscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'suback', packetid: 3 }, 
                },
                expected: {
                    'result': 'Unable to subscribe: payload not provided'
                }
            },
            {
                description: 'Testing subscribe, payload has illegal return code 3',
                method: 'subscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'suback', packetid: 4 }, 
                    payload: '[3]'
                },
                expected: {
                    'result': 'Unable to subscribe: illegal return codes in payload'
                }
            },
            {
                description: 'Testing subscribe, success',
                method: 'subscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'suback', packetid: 5 }, 
                    payload: '[0]'
                },
                expected: {
                    'result': [0],
                    'history': [
                        {
                            'path': 'put',
                            'method': '/subscribe',
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'packetid': '5',
                                'version': '1.0'
                            },
                            'payload': {
                                'clientId': 'test-client',
                                'topics': {
                                    '/test1/1': 0
                                }
                            }
                        }
                    ]
                }
            },
        ]
    },
    {
        description: 'Unsubscribe test',
        tests: [
            {
                description: 'Testing unsubscribe, suback missing',
                method: 'unsubscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'connack' }, 
                },
                expected: {
                    'result': 'Unable to unsubscribe: acknowledge \'unsuback\' expected, got connack'
                }
            },
            {
                description: 'Testing unsubscribe, packetid missing',
                method: 'unsubscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'unsuback' }, 
                },
                expected: {
                    'result': 'Unable to unsubscribe: wrong packet id expected: 2, got undefined'
                }
            },
            {
                description: 'Testing unsubscribe, payload missing',
                method: 'unsubscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'unsuback', packetid: 3 }, 
                },
                expected: {
                    'result': 'Unable to unsubscribe: payload not provided'
                }
            },
            {
                description: 'Testing unsubscribe, payload has illegal return code 3',
                method: 'unsubscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'unsuback', packetid: 4 }, 
                    payload: '[3]'
                },
                expected: {
                    'result': 'Unable to unsubscribe: illegal return codes in payload'
                }
            },
            {
                description: 'Testing unsubscribe, success',
                method: 'unsubscribe',
                args: ['1.0', {'/test1/1': 0}],
                result: { 
                    statusCode: 200, 
                    headers: { 'content-type': 'application/json', packet: 'unsuback', packetid: 5 }, 
                    payload: '[0]'
                },
                expected: {
                    'result': [0],
                    'history': [
                        {
                            'path': 'put',
                            'method': '/unsubscribe',
                            'headers': {
                                'content-type': 'application/json; charset=UTF-8',
                                'accept': 'application/json,text/plain',
                                'accept-charset': 'UTF-8',
                                'packetid': '5',
                                'version': '1.0'
                            },
                            'payload': {
                                'topics': {
                                    '/test1/1': 0
                                }
                            }
                        }
                    ]
                }
            },
        ]
    },
]
