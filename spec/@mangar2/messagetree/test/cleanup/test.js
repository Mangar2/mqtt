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
const VERBOSE = true

const testRun = new TestRun(VERBOSE)

/**
 * Creates a message tree from a test case definition
 * @param {Object} nodes nodes to add to the message tree
 */
function createTree(nodes) {
    const messageTree = new MessageTree({})
    for (const index in nodes) {
        const messageData = [...nodes[index]]
        const time = new Date()
        time.setHours(0, 0, 0, 1)
        time.setDate(time.getDate() - messageData[3])
        messageData[3] = time
        const message = new Message(...messageData)
        messageTree.addData(message, time)
    }
    return messageTree
}

/**
 * Prepare a test case
 */
testRun.on('prepare', (testCase) => {
    const messageTree = createTree(testCase.nodes)
    return messageTree
})

const runTest = (test, messageTree) => {
    messageTree.cleanup(test.daysWithoutUpdate)
    return messageTree
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
testRun.on('validate', (test, messageTree, path) => {
    let isCorrect = true
    const expectedTree = createTree(test.expected)
    isCorrect = testRun.unitTest.assertDeepEqual(messageTree, expectedTree, path)
    if (!isCorrect) {
        testRun.runAgain()
    }
})

function test () {
    testRun.run([
        'trees'
    ], __dirname,
    9)
}

test()
