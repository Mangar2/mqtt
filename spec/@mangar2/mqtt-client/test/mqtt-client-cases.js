module.exports = [
    {
        description: 'running client',
        clientId: 'test-client',
        tests: [
            {
                method: 'run',
                description: 'call run',
                expected: {
                    history: [
                        {
                            method: 'connect',
                            version: '1.0',
                            clean: true,
                            clientId: 'test',
                            host: 'localhost',
                            keepAlive: 0,
                            qos: 1
                        } 
                    ]
                }
            },
            {
                method: 'close',
                description: 'call close',
                expected: {
                    history: [
                        {
                            method: 'pingreq',
                            token: 'abcd'
                        },
                        {
                            method: 'publish',
                            message: {
                                topic: '$SYS/test/memory usage'
                            },
                            dup: false
                        },
                        {
                            method: 'disconnect',
                            clientId: 'test'
                        } 
                    ]
                }
            }
        ]
    },
    {
        description: 'publishing',
        clientId: 'test-client',
        tests: [
            {
                method: 'run',
                description: 'call run',
                expected: {}
            },
            {
                method: 'publish',
                description: 'call publish 1',
                message: ['a/b', 'test-message'],
                expected: {
                    history: [
                        {
                            method: 'pingreq',
                            token: 'abcd'
                        },
                        {
                            method: 'publish',
                            message: {
                                topic: 'a/b',
                                value: 'test-message',
                                qos: 1,
                                retain: false
                            },
                            dup: false
                        }
                    ]
                }
            },
            {
                method: 'publish',
                description: 'call publish 2',
                message: ['a/c', 'test-message'],
                expected: {
                    history: [
                        {
                            method: 'publish',
                            message: {
                                topic: '$SYS/test/memory usage'
                            },
                            dup: false
                        },
                        {
                            method: 'publish',
                            message: {
                                topic: 'a/c',
                            },
                            dup: false
                        }
                    ]
                }
            },
            {
                method: 'close',
                description: 'call close',
                expected: {}
            }
        ]
    },
    {
        description: 'subscribing',
        clientId: 'test-client',
        tests: [
            {
                method: 'run',
                description: 'call run',
                expected: {}
            },
            {
                method: 'subscribe',
                description: 'subscribe to a/b',
                args: ['test-client', { 'a/b': 1 }],
                expected: {
                    result: [1],
                    history: [
                        {
                            method: 'pingreq',
                            token: 'abcd'
                        },
                        {
                            clientId: 'test',
                            topics: {
                                'a/b': 1
                            },
                            method: 'subscribe'
                        }
                    ]
                }
            },
            {
                method: 'subscribe',
                description: 'subscribe to a/c',
                args: ['test-client', { 'a/c': 1, 'a/d': 2 }],
                expected: {
                    result: [1, 2],
                    history: [
                        {
                            method: 'publish',
                            message: {
                                topic: '$SYS/test/memory usage'
                            },
                            dup: false
                        },
                        {
                            clientId: 'test',
                            topics: {
                                'a/c': 1
                            },
                            method: 'subscribe'
                        }
                    ]
                }
            },
            {
                method: 'close',
                description: 'call close',
                expected: {}
            }
        ]
    }
]