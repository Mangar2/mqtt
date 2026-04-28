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
const { types } = require('@mangar2/utils')

const VERBOSE = false

const testRun = new TestRun(VERBOSE)
const testStart = new Date().getTime()

/**
 * Delays a test for a while
 * @param {number} milliseconds amount of milliseconds
 */
const delaySync = (milliseconds) => {
    const waitTill = new Date(new Date().getTime() + milliseconds)
    while (waitTill > new Date()) {
        // Intentionally left empty. Blocks the code for some time
    }
}

/**
 * Prepare a test case
 */
testRun.on('prepare', (testCase) => {
    const config = testCase.config !== undefined ? testCase.config : {}
    const messageTree = new MessageTree(config)
    for (const index in testCase.add) {
        if (testCase.delay) {
            delaySync(testCase.delay[index])
        }
        const messageData = testCase.add[index]
        const message = new Message(...messageData)
        messageTree.addData(message)
    }
    return messageTree
})

/**
 * Reruns a failed test for debugging
 */
testRun.on('break', (test, messageTree) => {
    const node = messageTree.getNode(test.getNode)
    return node
})

/**
 * Runs the test
 */
testRun.on('run', (test, messageTree) => {
    const node = messageTree.getNode(test.getNode)
    return node
})

/**
 * Check if a timestamp is valid (between test start and current time)
 * @param {number} timestamp timestamp to check
 */
const checkTimestamp = (timestamp) => {
    const now = new Date().getTime()
    return (timestamp >= testStart && timestamp <= now)
}

/**
 * Checks a property
 * @param {string|number|Array} expected expected value
 * @param {string|number|Array} valueToTest
 * @param {string} property name of the property
 * @param {string} path test description
 * @returns {boolean} true, if the property is as expected
 */
const checkProperty = (expected, valueToTest, property, path) => {
    let result = true
    if (types.isArray(expected)) {
        result = testRun.unitTest.assertEqual(expected.length, valueToTest.length, path + '/length')
        for (const index in expected) {
            result = result &&
                checkProperty(expected[index], valueToTest[index], property, path + '/' + index)
        }
    } else if (property === 'reason') {
        result = testRun.unitTest.assertEqual(expected, valueToTest[0].message, path + '/reason')
    } else if (expected === 'timestamp') {
        result = testRun.unitTest.assertTrue(checkTimestamp(valueToTest), path + '/timestamp')
    } else {
        result = testRun.unitTest.assertEqual(expected, valueToTest, path)
    }
    return result
}

/**
 * Checks if the history matches the expected result
 * @param {Object} expected expected history structure
 * @param {Object} history found history structure
 * @param {string} path test path information
 */
const checkHistory = (expected, history, path) => {
    let result = true
    result = testRun.unitTest.assertEqual(expected.length, history.length, path + '/length')
    for (const index in history) {
        const entry = history[index]
        const expectedHistory = expected[index]
        for (const property in expectedHistory) {
            result = result &&
                checkProperty(expectedHistory[property], entry[property], property, path + '/' + index)
        }
    }
    return result
}

/**
 * Validates the test result
 */
testRun.on('validate', (test, node, path) => {
    let isCorrect = true
    if (test.expected === 'undefined') {
        isCorrect = testRun.unitTest.assertEqual(node, undefined, path)
    } else {
        for (const property in test.expected) {
            let testResult
            const expected = test.expected[property]
            if (property === 'history') {
                testResult = checkHistory(expected, node.history, path + '/' + property)
            } else {
                testResult = checkProperty(expected, node[property], property, path + '/' + property)
            }
            isCorrect = isCorrect && testResult
        }
    }
    if (!isCorrect) {
        console.log('expected')
        console.log(JSON.stringify(test.expected, null, 2))
        console.log('got')
        console.log(JSON.stringify(node, null, 2))
        testRun.runAgain()
    }
})

function testMessageTree () {
    testRun.run([
        'split',
        'addmessage',
        'errorcases',
        'history',
        'timevalue',
        'time',
        'interval',
        'historyoverflow'
    ],
    __dirname,
    283)
}

testMessageTree()

console.log('test getSection')
require('./getSection/test')

console.log('test getNodes')
require('./getNodes/test')

console.log('test cleanup')
require('./cleanup/test')

console.log('test large tree')
require('./largeTree/test')

