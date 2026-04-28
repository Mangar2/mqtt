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
const { deriveSubscribes } = require('../../derivesubscribes')

const VERBOSE = false
const testrunToSerial = new TestRun(VERBOSE)

testrunToSerial.on('prepare', testcase => {
})

const runTest = (test) => {
    const subscribes = deriveSubscribes(test)
    return subscribes
}

testrunToSerial.on('run', runTest)

testrunToSerial.on('break', (test) => {
    runTest(test)
})

testrunToSerial.on('validate', (test, subscribes, path) => {
    let validate = true
    if (!testrunToSerial.unitTest.assertDeepEqual(test.expected, subscribes, path)) {
        validate = false
    }
    if (!validate) {
        console.log('expected:')
        console.log(test.expected)
        console.log('found:')
        console.log(subscribes)
        testrunToSerial.runAgain()
    }
})

testrunToSerial.run(
    [
        'subscribes'
    ],
    __dirname,
    7
)
