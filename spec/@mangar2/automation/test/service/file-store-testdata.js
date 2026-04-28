module.exports = [
    {
        description: 'update rules',
        config: {
            subscribeQoS: 2,
            longitude: 0.001545,
            latitude: 51.477928,
            rules: [],
            filestore: {
                use: true
            }
        },
        rules: {
            a: {
                rules: {
                    rule1: {
                        topic: { test: 0 }
                    }
                }
            }
        },
        tests: [
            {
                description: 'rules unchanged',
                expected: {
                    rules: {
                        a: {
                            rules: {
                                rule1: {
                                    topic: { test: 0 }
                                }
                            }
                        }
                    }
                }
            },
            {
                description: 'additional rule',
                message:
                {
                    topic: '$SYS/automation/rules/a/rule2/set',
                    value: JSON.stringify({
                        topic: { test: 1 }
                    })
                },
                delay: 1000,
                expected: {
                    rules: {
                        a: {
                            rules: {
                                rule1: {
                                    topic: { test: 0 }
                                },
                                rule2: {
                                    topic: { test: 1 }
                                }
                            }
                        }
                    }
                }
            }
        ]
    }
]
