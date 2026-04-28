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
const types = require('@mangar2/types')
const Decision = require('@mangar2/decision')
const TestRun = require('@mangar2/testrun')
const { isTimeOfDayString, timeOfDayStringToDate } = require('@mangar2/time')

const testRun = new TestRun(VERBOSE)

/**
 * Tests all kind of operators
 */
function testCompareOperators () {
    const variables = {
        'a/b': 1,
        'b/c': 2,
        'system/presence': 'awake'
    }

    const decision = new Decision(variables)
    const tests = [
        { operators: ['equal', 'equals', '='], variables: { 'a/b': 1, 'b/c': 2 }, result: true },
        { operators: ['equal', 'equals', '='], variables: { 'a/b': 0, 'b/c': 1 }, result: false },
        { operators: ['not equal', 'not equals', '!=', '<>'], variables: { 'a/b': 1, 'b/c': 2 }, result: false },
        { operators: ['not equal', 'not equals', '!=', '<>'], variables: { 'a/b': 0, 'b/c': 1 }, result: true },
        { operators: ['below', 'less', '<'], variables: { 'a/b': 0, 'b/c': 1 }, result: false },
        { operators: ['below', 'less', '<'], variables: { 'a/b': 1, 'b/c': 2 }, result: false },
        { operators: ['below', 'less', '<'], variables: { 'a/b': 2, 'b/c': 3 }, result: true },
        { operators: ['above', 'greater', '>'], variables: { 'a/b': 0, 'b/c': 1 }, result: true },
        { operators: ['above', 'greater', '>'], variables: { 'a/b': 1, 'b/c': 2 }, result: false },
        { operators: ['above', 'greater', '>'], variables: { 'a/b': 2, 'b/c': 3 }, result: false },
        { operators: ['below or equal', 'less or equal', '<='], variables: { 'a/b': 0, 'b/c': 1 }, result: false },
        { operators: ['below or equal', 'less or equal', '<='], variables: { 'a/b': 1, 'b/c': 2 }, result: true },
        { operators: ['below or equal', 'less or equal', '<='], variables: { 'a/b': 2, 'b/c': 3 }, result: true },
        { operators: ['above or equal', 'greater or equal', '>='], variables: { 'a/b': 0, 'b/c': 1 }, result: true },
        { operators: ['above or equal', 'greater or equal', '>='], variables: { 'a/b': 1, 'b/c': 2 }, result: true },
        { operators: ['above or equal', 'greater or equal', '>='], variables: { 'a/b': 2, 'b/c': 3 }, result: false }
    ]
    for (const test of tests) {
        for (const operator of test.operators) {
            for (const variable in test.variables) {
                const value = test.variables[variable]
                const term = [operator, variable, value]
                const result = test.result
                const evalResult = decision.eval(term)
                testRun.unitTest.assertEqual(evalResult.value, result, evalResult.reason)
            }
        }
    }
}

testCompareOperators()

testRun.on('prepare', (testCase) => {
    const variables = testCase.variables
    for (const variable in variables) {
        if (isTimeOfDayString(variables[variable])) {
            variables[variable] = timeOfDayStringToDate(variables[variable])
        }
    }
    const decision = new Decision(variables)
    decision.on('test', (param) => {
        return param
    })
    return decision
})

testRun.on('break', (test, decision) => {
    decision.eval(test.tree)
})

testRun.on('run', (test, decision) => {
    let result
    try {
        result = decision.eval(test.tree)
    } catch (err) {
        result = { value: false, reason: err.message }
    }
    return result
})

testRun.on('validate', (test, result, path) => {
    const value = types.getType(result.value) === 'Date' ? result.value.toLocaleTimeString() : result.value
    if (value !== test.result) {
        testRun.runAgain()
    }
    testRun.unitTest.assertEqual(value, test.result, path)
    if (test.reason !== undefined) {
        testRun.unitTest.assertEqual(result.reason, test.reason, path)
    } else {
        testRun.unitTest.log(result.reason)
    }
})

testRun.run([
    'strings',
    'if',
    'and_or',
    'math',
    'switch',
    'complex',
    'callback'
], __dirname)

testRun.unitTest.showResult(164)
