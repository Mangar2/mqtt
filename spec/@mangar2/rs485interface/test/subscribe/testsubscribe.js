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

const TestRun = require('@mangar2/testrun')
const subscribe = require('../../derivesubscribes')

const VERBOSE = false
const testrun = new TestRun(VERBOSE)

testrun.on('prepare', testcase => {
})

const runTest = (test, actions) => {
    return subscribe(test.options)
}

testrun.on('run', runTest)

testrun.on('break', (test, actions) => {
    runTest(test, actions)
})

testrun.on('validate', (test, result, path) => {
    const validate = testrun.unitTest.assertDeepEqual(result, test.expected, path)
    if (!validate) {
        const output = JSON.stringify(result, null, 2)
        console.log(output)
        testrun.runAgain()
    }
})

testrun.asyncRun(
    [
        'subscribe'
    ],
    __dirname,
    1
)
