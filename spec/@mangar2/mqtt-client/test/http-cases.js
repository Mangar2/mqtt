module.exports = [
    {
        description: 'Http test',
        clientId: 'test-client',
        tests: [
            {
                description: 'Testing publish',
                expected: {
                    result: [
                        'delivered (qos 1)'
                    ],
                    history: [
                        {
                            message: {
                                topic: 'a/b',
                                value: 'hello world',
                                qos: 1,
                                retain: false
                            },
                            dup: false
                        }
                    ]
                }
            }
        ]
    }
]