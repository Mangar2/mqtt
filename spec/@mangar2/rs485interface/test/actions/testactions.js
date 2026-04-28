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
const Actions = require('../../actions')
const Message = require('@mangar2/message')
const { delay } = require('@mangar2/utils')

const VERBOSE = false
const testrun = new TestRun(VERBOSE)
let resultMessages

testrun.on('prepare', testcase => {
    const actions = new Actions(testcase.options)
    actions.storeState(testcase.messages)
    return actions
})

const runTest = async (test, actions) => {
    resultMessages = []
    actions.processMessage(test.actionMessage, resultMessage => {
        const messageCopy = new Message(resultMessage.topic, resultMessage.value, resultMessage.reason)
        resultMessages.push(messageCopy)
    })
    await delay(test.delay, VERBOSE)
    return resultMessages
}

testrun.on('run', runTest)

testrun.on('break', async (test, actions) => {
    runTest(test, actions)
})

testrun.on('validate', async (test, testResultMessages, path) => {
    let validate = testrun.unitTest.assertEqual(test.expected.length, testResultMessages.length, path + '/ length')
    for (const index in test.expected) {
        const expectedMessage = test.expected[index]
        const resultMessage = testResultMessages[index]
        for (const property in expectedMessage) {
            const expectedProperty = expectedMessage[property]
            const resultProperty = resultMessage[property]
            if (!testrun.unitTest.assertEqual(expectedProperty, resultProperty, path + '/' + index + '/' + property)) {
                validate = false
            }
        }
    }
    if (!validate) {
        console.log(JSON.stringify(testResultMessages, null, 2))
        testrun.runAgain()
    }
})

testrun.asyncRun(
    [
        'actions'
    ],
    __dirname,
    41
)
