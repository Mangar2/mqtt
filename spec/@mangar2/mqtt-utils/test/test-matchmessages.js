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

const { Message, MatchMessages } = require('../dist/index')
const { TestRun } = require('@mangar2/unittest')

const VERBOSE = false
const testrun = new TestRun(VERBOSE)

testrun.on('prepare', testcase => {
    const matchMessage = new MatchMessages()
    for (const incomingMessage of testcase.incomingMessages) {
        const mqttMessage = new Message(...incomingMessage)
        matchMessage.addReceivedMessage(mqttMessage)
    }
    return matchMessage
})

const runTest = (test, matchMessage) => {
    const outgoing = new Message(...test.outgoingMessage)
    const result = matchMessage.matchAndUpdateReplyMessage(outgoing)
    return result
}

testrun.on('run', runTest)

testrun.on('break', (test, message) => {
    runTest(test, message)
})

testrun.on('validate', (test, resultMessage, path) => {
    let validate = true
    const expectedMessage = test.expected
    for (const property in expectedMessage) {
        const expected = expectedMessage[property]
        const resultProperty = resultMessage[property]
        if (property === 'reason') {
            if (!testrun.unitTest.assertEqual(expected.length, resultProperty.length, path + '/reason/length')) {
                validate = false
            } else {
                for (const index in expected) {
                    const expectedReason = expected[index]
                    const resultReason = resultMessage.reason[index].message
                    if (!testrun.unitTest.assertEqual(expectedReason, resultReason, path + '/reason/' + index)) {
                        validate = false
                    }
                }
            }
        } else if (!testrun.unitTest.assertEqual(expected, resultProperty, path + '/' + property)) {
            validate = false
        }
    }
    if (!validate) {
        console.log(resultMessage)
        testrun.runAgain()
    }
})

module.exports = () => testrun.asyncRun( ['test-matchmessages-cases' ], __dirname, 54 )
