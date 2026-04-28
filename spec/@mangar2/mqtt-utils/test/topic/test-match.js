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

const { TopicMatch } = require('../../dist/index')
const { TestRun } = require('@mangar2/unittest')

const VERBOSE = false
const testrunTopicMatch = new TestRun(VERBOSE)

testrunTopicMatch.on('prepare', testcase => {
    const topicMatch = new TopicMatch()
    topicMatch.addPattern(testcase.pattern, 0)
    return topicMatch
})

const runTest = (test, topicMatch) => {
    const firstMatch = topicMatch.getFirstMatch(test.topic)
    const result = firstMatch !== undefined
    return result
}

testrunTopicMatch.on('run', runTest)

testrunTopicMatch.on('break', (test, topicMatch) => {
    runTest(test, topicMatch)
})

testrunTopicMatch.on('validate', (test, firstMatch, path) => {
    const validate = testrunTopicMatch.unitTest.assertEqual(firstMatch, test.expected, path)
    if (!validate) {
        console.log(firstMatch)
        console.log(test.matches)
        testrunTopicMatch.runAgain()
    }
})

module.exports = () => testrunTopicMatch.run(['test-match-cases'], __dirname, 66)
