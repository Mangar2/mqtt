module.exports = [
    {
        description: 'initial tree',
        ruleTree: {
            one: { 
                two1: { rules: { r1: { value: 'a/value', topic: 'r1' } } }
            }
        },
        tests: [
            {
                description: 'value undefined',
                check: true,
                expected: { 
                    invalid: [],
                    messages: [{
                        topic: '$SYS/automation/rules/one/two1/r1',
                        value: 'Error in rule \'one/two1/r1\':  \'a/value\' is undefined'
                    }]
                }
            },
            {
                description: 'value defined',
                check: true,
                variables: {
                    'a/value': 'a value'
                },
                expected: {
                    messages: [],
                    rules: [{
                        name: 'one/two1/r1',
                        error: undefined
                    }]
                }
            }
        ]
    },
]