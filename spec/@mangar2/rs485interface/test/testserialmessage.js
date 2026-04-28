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

const { types } = require('@mangar2/utils')
const TestRun = require('@mangar2/testrun')
const SerialMessage = require('../serialmessage')

const VERBOSE = false
const testrun = new TestRun(VERBOSE)

testrun.on('prepare', testcase => {
    const serialMessage = new SerialMessage(testcase.version)
    return serialMessage
})

const runTest = (test, message) => {
    const result = {}
    try {
        result.message = message
        message.setFromByteArray(test.serialData, test.startIndex)
    } catch (err) {
        result.err = err
    }
    return result
}

testrun.on('run', runTest)

testrun.on('break', (test, message) => {
    runTest(test, message)
})

testrun.on('validate', (test, result, path) => {
    let validate = true
    const { message, err } = result
    if (types.isObject(err) || test.errorMessage !== undefined) {
        validate = testrun.unitTest.assertTrue(err.message.startsWith(test.errorMessage), path + '/error')
    } else {
        for (const property in test.expected) {
            const expected = test.expected[property]
            if (!testrun.unitTest.assertEqual(expected, message[property], path + '/' + property)) {
                validate = false
            }
        }
        const byteArray = message.getByteArray()
        for (let index = 0; index < byteArray.length; index++) {
            const byteArrayValue = byteArray[index]
            if (!testrun.unitTest.assertEqual(test.serialData[index + test.startIndex], byteArrayValue, path + '/' + index)) {
                validate = false
            }
        }
    }
    if (!validate) {
        if (err !== undefined) {
            console.log(err.message)
        } else {
            console.log(message)
            testrun.runAgain()
        }
    }
})

testrun.asyncRun(
    [
        'serial/serialmessagev0',
        'serial/serialmessagev1'
    ],
    __dirname,
    177
)
