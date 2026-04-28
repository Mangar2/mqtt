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
const ReadMessages = require('../readmessages')

const VERBOSE = false
const testrun = new TestRun(VERBOSE)

testrun.on('prepare', testcase => {
    const readMessages = new ReadMessages()
    return readMessages
})

const runTest = (test, readMessages) => {
    const result = {}
    try {
        const buffer = Buffer.from(test.serialData)
        result.messages = readMessages.read(buffer)
    } catch (err) {
        result.err = err
    }
    return result
}

testrun.on('run', runTest)

testrun.on('break', (test, readMessages) => {
    runTest(test, readMessages)
})

testrun.on('validate', (test, result, path) => {
    let validate = true
    const { messages } = result
    for (const index in test.expected) {
        const expectedData = test.expected[index]
        const resultData = messages[index]
        if (types.isObject(resultData) && types.isString(resultData.error) && types.isString(expectedData)) {
            const resultError = resultData.error
            validate = testrun.unitTest.assertTrue(resultError.startsWith(expectedData), path + '/error')
        } else if (resultData === undefined || !types.isObject(resultData.message)) {
            validate = false
            testrun.unitTest.fail(path + '/' + index)
        } else {
            const resultMessage = resultData.message
            for (const property in expectedData) {
                const expectedValue = expectedData[property]
                if (!testrun.unitTest.assertEqual(expectedValue, resultMessage[property], path + '/' + property)) {
                    validate = false
                }
            }
        }
        if (validate === false) {
            break
        }
    }
    if (!validate) {
        console.log(JSON.stringify(result, null, 2))
        testrun.runAgain()
    }
})

testrun.asyncRun(
    [
        'read/readmessage'
    ],
    __dirname,
    27
)
