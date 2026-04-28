module.exports = [
    {
        description: 'simple',
        config: {
            subscribeQoS: 2,
            longitude: 0.001545,
            latitude: 51.477928,
            rules: [],
            filestore: {
                use: false
            }
        },
        tests: [
            {
                description: 'default subscriptions',
                expected: {
                    subscriptions: {
                        '+/+/+/motion sensor/detection state': 2,
                        '$SYS/presence/set': 2,
                        '$SYS/simulation/#': 1,
                        '$SYS/automation/#': 1
                    }
                }
            },
            {
                description: 'no rules no messages',
                expected: {
                    messages: [] 
                }
            },
            {
                description: 'set rule',
                rules: {
                    rules: {
                        rule1: {
                            topic: { test: 0 }
                        }
                    }
                },
                expected: {
                    automation: {
                        _rules: {
                            _rules: { rule1: { topic: { test: 0 }, name: 'rule1' } }
                        }
                    },
                    messages: [
                        {
                            topic: 'test',
                            value: 0,
                            qos: 1
                        }
                    ]
                }
            },
            {
                description: 'handle message',
                rules: {
                    rules: {
                        rule2: {
                            topic: { 'new/variable': 'new/variable/set'  }
                        }
                    }
                },
                message: {
                    topic: 'new/variable/set',
                    value: 1
                },
                expected: {
                    messages: [
                        {
                            topic: 'new/variable',
                            value: 1
                        }
                    ]
                }
            },
            {
                description: 'send rule without set',
                rules: {
                    rules: {
                        rule3: {
                            topic: { 'new/variable': 1  }
                        }
                    }
                },
                message: {
                    topic: '$SYS/automation/rules/rule3',
                    value: {
                        active: true,
                        topic: { 'new/variable': 2  }
                    }
                },
                expected: {
                    messages: [
                        {
                            topic: 'new/variable',
                            value: 1
                        }
                    ]
                }
            },
            {
                description: 'update rule',
                rules: {
                    rules: {
                        rule3: {
                            topic: { 'new/variable': 1  }
                        }
                    }
                },
                message: {
                    topic: '$SYS/automation/rules/rule3/set',
                    value: JSON.stringify({
                        active: true,
                        topic: { 'new/variable': 2  }
                    })
                },
                expected: {
                    messages: [
                        {
                            topic: 'new/variable',
                            value: 2
                        },
                        {
                            topic: '$SYS/automation/rules/rule3',
                            value: JSON.stringify({
                                active: true,
                                topic: { 'new/variable': 2 },
                                name: 'rule3',
                                isValid: true
                            }),
                            reason: [
                                {
                                    message: 'update message'
                                }
                            ],
                            qos: 1
                        }
                    ]
                }
            },
            {
                description: 'delete rule',
                rules: {
                    rules: {
                        rule4: {
                            topic: { 'new/variable': 1  }
                        }
                    }
                },
                message: {
                    topic: '$SYS/automation/rules/rule4/set',
                    value: 'delete'
                },
                expected: {
                    messages: [
                        {
                            topic: '$SYS/automation/rules/rule4',
                            value: 'deleted'
                        }
                    ]
                }
            }
        ]
    }
]
