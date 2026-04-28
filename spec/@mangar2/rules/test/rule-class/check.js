module.exports = [
    {
        description: 'undefined member',
        ruleTree: {
        },
        tests: [
            {
                description: 'dontExists',
                rule: { name: 'one', topic: 'topic', dontExist: 'anything' },
                check: true,
                expected: { 
                    rules: [ { topic: 'topic' } ], 
                    tree: { rules: { one: { topic: 'topic' } } },
                    invalid: [
                        { 
                            topic: 'topic', 
                            name: 'one', 
                            dontExist: 'anything', 
                            isValid: false, 
                            errors: { dontExist: 'unknown property, misspelled?'} 
                        } 
                    ],
                    messages: []
                }
            }
        ]
    },
    {
        description: 'variables',
        ruleTree: {
        },
        tests: [
            {
                description: 'internal variable',
                rule: { name: 'one', topic: 'topic', value: '/time' },
                check: true,
                expected: { 
                    invalid: [],
                    variables: { }
                }
            },
            {
                description: 'undefined variable',
                rule: { name: 'one', topic: 'topic', value: 'variable/not/defined' },
                check: true,
                expected: { 
                    invalid: [],
                    variables: { 'variable/not/defined2': undefined },
                    messages: [{
                        topic: '$SYS/automation/rules/one',
                        value: 'Error in rule \'one\':  \'variable/not/defined\' is undefined'
                    }]
                }
            },
            {
                description: 'defined variable',
                rule: { name: 'one', topic: 'topic', value: ['+', 'variable/defined', 10] },
                variables: {
                    'variable/defined': 10
                },
                check: true,
                expected: { 
                    invalid: [],
                    variables: { 'variable/defined': 10 }
                }
            }
        ]
    }
]