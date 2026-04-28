module.exports = [
    {
        description: 'Subscribe test',
        clientId: 'test-client',
        result: [1, 0],
        tests: [
            {
                description: 'Testing subscribe method, parameter is not an object',
                method: 'subscribe',
                args: ['1.0', ['/a/b/c', '/a/b/d']],
                result: {
                    payload: '{ }'
                },
                expected: {
                    'result': 'Unable to subscribe: Unable to subscribe: {"topics":"types does not match, required is: [\\"object\\"]"}'
                }
            },
            {
                description: 'Testing subscribe, success',
                method: 'subscribe',
                args: ['1.0', { '/a/b/c': 1, '/a/b/d': 0 }],

                expected: {
                    result: [1, 0],
                    history: [
                        {
                            clientId: 'test-client',
                            topics: {
                                '/a/b/c': 1,
                                '/a/b/d': 0
                            }
                        }
                    ]
                }
            }
        ]
    },
    {
        description: 'Unsubscribe test',
        clientId: 'test-client',
        result: [0x11, 0],
        tests: [
            {
                description: 'Testing unsubscribe method, parameter is not an array',
                method: 'unsubscribe',
                args: ['1.0', {'/a/b/c': 0, '/a/b/d': 1}],
                result: {
                    payload: '{ }'
                },
                expected: {
                    'result': 'Unable to unsubscribe: Unable to unsubscribe: {"topics":"types does not match, required is: [\\"array\\"]"}'
                }
            },
            {
                description: 'Testing unsubscribe, success',
                method: 'unsubscribe',
                args: ['1.0', [ '/a/b/c', '/a/b/d' ]],

                expected: {
                    result: [0x11, 0],
                    history: [
                        {
                            clientId: 'test-client',
                            topics: ['/a/b/c', '/a/b/d']
                        }
                    ]
                }
            }
        ]
    },
]
