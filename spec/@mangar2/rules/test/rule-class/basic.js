module.exports = [
    {
        description: 'initial tree',
        ruleTree: {
            one: { 
                two1: { rules: { r1: { topic: 'r1' }, r2: { topic: 'r2' }}},
                two2: { rules: { r3: { topic: 'r3' }, r4: { topic: 'r4' }}}
            }
        },
        tests: [
            {
                description: 'unchanged',
                expected: { 
                    rules: [ 
                        { topic: 'r1', name: 'one/two1/r1' }, 
                        { topic: 'r2', name: 'one/two1/r2' }, 
                        { topic: 'r3', name: 'one/two2/r3' }, 
                        { topic: 'r4', name: 'one/two2/r4' } 
                    ], 
                    tree: {
                        one: { 
                            two1: { rules: { r1: { topic: 'r1' }, r2: { topic: 'r2' }}},
                            two2: { rules: { r3: { topic: 'r3' }, r4: { topic: 'r4' }}}
                        }
                    }, 
                    invalid: [] 
                }
            }
        ]
    },
    {
        description: 'add rules',
        ruleTree: { },
        tests: [
            {
                description: 'empty',
                expected: { rules: [], tree: {}, invalid: [] }
            },
            {
                description: 'set',
                rule: { name: 'hello', topic: 'world' },
                expected: { 
                    rules: [{ name: 'hello', topic: 'world' }], 
                    tree: { 'rules': { 'hello': { name: 'hello' } } }, 
                    invalid: [] 
                }
            },
            {
                description: 'set chunks in name',
                rule: { name: 'one/two/three', topic: 'world' },
                expected: { 
                    rules: [{ name: 'hello', topic: 'world' }, { name: 'one/two/three', topic: 'world' }], 
                    tree: {
                        'rules': { 'hello': { name: 'hello', topic: 'world'} },
                        'one': { 'two' : { 'rules': { 'three': { name: 'one/two/three' } } } } 
                    }, 
                    invalid: [] 
                }
            },
            {
                description: 'extend tree at rule node',
                rule: { name: 'one/two/four', topic: 'four' },
                expected: { 
                    rules: [
                        { name: 'hello', topic: 'world' }, 
                        { name: 'one/two/three', topic: 'world' } , 
                        { name: 'one/two/four', topic: 'four' }
                    ], 
                    tree: {
                        rules: { hello: { name: 'hello', topic: 'world'} },
                        one: { two : { 'rules': { 
                            three: { name: 'one/two/three' },
                            four: { name: 'one/two/four' }
                        } } } 
                    }, 
                    invalid: [] 
                }
            },
            {
                description: 'extend tree at root node',
                rule: { name: 'two/three', topic: 'five' },
                expected: { 
                    tree: {
                        rules: { hello: { name: 'hello', topic: 'world'} },
                        one: { two : { rules: { 
                            three: { name: 'one/two/three' },
                            four: { name: 'one/two/four' }
                        } } },
                        two: { rules: { three: { name: 'two/three' }}}
                    }, 
                    invalid: [] 
                }
            }                  
        ]
    },
    {
        description: 'updates',
        ruleTree: {
            one: { 
                two1: { rules: { r1: { topic: 'r1' }, r2: { topic: 'r2' }}},
                two2: { rules: { r3: { topic: 'r3' }, r4: { topic: 'r4' }}}
            }
        },
        tests: [
            {
                description: 'update topic',
                rule: { name: 'one/two1/r1', topic: 'new' },
                expected: { 
                    tree: {
                        one: { 
                            two1: { rules: { r1: { topic: 'new' }, r2: { topic: 'r2' }}},
                        }
                    } 
                }
            }
        ]
    },
    {
        description: 'delete',
        ruleTree: {
            one: { 
                two1: { rules: { r1: { topic: 'r1' }, r2: { topic: 'r2' }}},
                two2: { rules: { r3: { topic: 'r3' } }}
            }
        },
        tests: [
            {
                description: 'delete first',
                delete: 'one/two1/r1',
                expected: { 
                    rules: [ 
                        { topic: 'r2', name: 'one/two1/r2' }, 
                        { topic: 'r3', name: 'one/two2/r3' } 
                    ], 
                    tree: {
                        one: { 
                            two1: { rules: { r1: undefined, r2: { topic: 'r2' }}},
                            two2: { rules: { r3: { topic: 'r3' }}}
                        }
                    }
                }
            },
            {
                description: 'delete second',
                delete: 'one/two1/r2',
                expected: { 
                    rules: [ 
                        { topic: 'r3', name: 'one/two2/r3' } 
                    ], 
                }
            },
            {
                description: 'delete last',
                delete: 'one/two2/r3',
                expected: { 
                    rules: []
                }
            }
        ]
    }
]