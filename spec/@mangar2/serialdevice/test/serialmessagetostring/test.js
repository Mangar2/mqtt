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
const { serialMessageToString } = require('../../serialmessagetostring')

const VERBOSE = false
const testrunToSerial = new TestRun(VERBOSE)

const runTest = (test) => {
    const message = test.message
    const result = serialMessageToString(message)
    return result
}

testrunToSerial.on('run', runTest)

testrunToSerial.on('break', (test) => {
    runTest(test)
})

testrunToSerial.on('validate', (test, serialString, path) => {
    const validate = testrunToSerial.unitTest.assertEqual(test.expected, serialString, path)
    if (!validate) {
        console.log(serialString)
        testrunToSerial.runAgain()
    }
})

testrunToSerial.asyncRun(
    [
        'toserialstring'
    ],
    __dirname,
    5
)
