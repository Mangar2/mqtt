/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */
'use strict'

const VERBOSE = false
const { Rules, ProcessRule } = require('@mangar2/rules') 

const Testrun = require('@mangar2/testrun')
const testrun = new Testrun(VERBOSE)


testrun.on('prepare', testcase => {
    const rules = new Rules(testcase.ruleTree)
    return rules
})

function runTest (test, rules) {
    if (test.rule) {
        rules.setRule(test.rule)
    }
    if (test.delete) {
        rules.deleteRule(test.delete)
    }
    const tree = rules.getRuleTree()
    let result = {
        rules: rules.getRules(),
        invalid: rules.getInvalidRules(),
        tree
    }
    if (test.check) {
        const processRule = new ProcessRule(new Date(), 0, 0)
        for (const name in test.variables) {
            const value = test.variables[name]
            processRule.setVariable(name, value)
        }
        const check = rules.checkRules(processRule)
        result = {...result, ...check}
    }
    return result
}

testrun.on('run', (test, rules) => {
    return runTest(test, rules)
})

testrun.on('break', (test, rules) => {
    runTest(test, rules)
})


testrun.run( [ 'basic', 'check', 'error' ], __dirname, 16, 'js')

