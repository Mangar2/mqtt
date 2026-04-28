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

const VERBOSE = false

const Message = require('@mangar2/message')
const EventHistory = require('../../eventHistory')

const Testrun = require('@mangar2/testrun')
const testrun = new Testrun(VERBOSE)

const massTest = (unitTest) => {
    const eventHistory = new EventHistory()
    for (let i = 0; i < 10000; i++) {
        eventHistory.addEvent(new Message('/room1/motion' + i, 1, 'motion detected', new Date()), true)
    }
    const longList = eventHistory.getLatestEvents()
    unitTest.assertTrue(Object.keys(longList.motions).length === 100)
}



const now = (new Date()).getTime()

/**
 * Returns a date based on the now timestamp and adds seconds
 * @param {number} seconds amount of seconds relative to "now"
 * @returns {Date} now + seconds
 */
function secondsToDate (seconds) {
    const time = new Date(now)
    time.setSeconds(seconds)
    return time
}

const runTest = (test) => {
    const eventHistory = new EventHistory()

    for (const messageData of test.motions) {
        const time = secondsToDate(messageData.seconds)
        const message = new Message(messageData.topic, messageData.value, messageData.reason, time)
        eventHistory.addEvent(message, true)
    }
    const result = eventHistory.getLatestEvents()
    return result
}

testrun.on('run', runTest)

testrun.on('break', (test) => {
    runTest(test)
})


testrun.on('validate', (test, result, path) => {
    for (const topic in test.expected.motions) {
        const time = secondsToDate(test.expected.motions[topic])
        test.expected.motions[topic] = time.getTime()
    }
    testrun.validate(test, result, path, false)
})


module.exports = () => {
    console.log('running history test')
    massTest(testrun.unitTest)
    testrun.run(['eventhistory'], __dirname, 9)
}
