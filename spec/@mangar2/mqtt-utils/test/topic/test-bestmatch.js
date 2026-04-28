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

const { TopicMatch } = require('@mangar2/mqtt-utils')
const { TestRun } = require('@mangar2/unittest')

const VERBOSE = false
const testrunTopicMatch = new TestRun(VERBOSE)

testrunTopicMatch.on('prepare', testcase => {
    const topicMatch = new TopicMatch()
    topicMatch.addPattern(testcase.pattern, 0)
    return topicMatch
})

const runTest = (test, topicMatch) => {
    return topicMatch.getBestMatch(test.topic)
}

testrunTopicMatch.on('run', runTest)

testrunTopicMatch.on('break', (test, topicMatch) => {
    runTest(test, topicMatch)
})

module.exports = () => testrunTopicMatch.run(['test-bestmatch-cases' ], __dirname, 8, 'js' )
