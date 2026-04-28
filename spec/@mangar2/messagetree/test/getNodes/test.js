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

const MessageTree = require('@mangar2/messagetree')
const TestRun = require('@mangar2/testrun')
const Message = require('@mangar2/message')
const VERBOSE = false

const testRun = new TestRun(VERBOSE)

/**
 * Prepare a test case
 */
testRun.on('prepare', (testCase) => {
    const config = testCase.config !== undefined ? testCase.config : {}
    const time = new Date(0)
    const messageTree = new MessageTree(config)
    for (const index in testCase.add) {
        const messageData = testCase.add[index]
        messageData[3] = time
        const message = new Message(...messageData)
        messageTree.addData(message, time)
    }
    return messageTree
})

const runTest = (test, messageTree) => {
    const section = messageTree.getNodes(
        test.nodes, test.addHistory, test.addReason)
    return section
}

/**
 * Reruns a failed test for debugging
 */
testRun.on('break', (test, messageTree) => {
    return runTest(test, messageTree)
})

/**
 * Runs the test
 */
testRun.on('run', (test, messageTree) => {
    return runTest(test, messageTree)
})

/**
 * Validates the test result
 */
testRun.on('validate', (test, section, path) => {
    let isCorrect = true
    if (test.expected === 'undefined') {
        isCorrect = testRun.unitTest.assertEqual(section, undefined, path)
    } else {
        isCorrect = testRun.unitTest.assertDeepEqual(section, test.expected, path)
    }
    if (!isCorrect) {
        console.log('got')
        console.log(JSON.stringify(section, null, 2))
        console.log('exected')
        console.log(JSON.stringify(test.expected, null, 2))
        testRun.runAgain()
    }
})

function test () {
    testRun.run([
        'nodes'
    ], __dirname,
    9)
}

test()
