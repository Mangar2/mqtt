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
const { Automation } = require('@mangar2/automation')
const { timeOfDayStringToDate } = require('@mangar2/time')
const Testrun = require('@mangar2/testrun')
const testrun = new Testrun(VERBOSE)

/**
 * Adds motions to the automation class
 * @param {Automation} automation automation class
 * @param {Object} motionList list of motions, format topic:timeOfDayString
 */
function setMotions (automation, motionList) {
    for (const motionTopic in motionList) {
        const motionTime = timeOfDayStringToDate(motionList[motionTopic])
        automation._processMessage(new Message(motionTopic, 1, 'test motion', motionTime))
    }
}

/**
 * Sends messages to the automation class to set variables
 * @param {Automation} automation automation class
 * @param {Object} variableList list of variable: value elements to set variables
 */
function sendMessages (automation, variableList) {
    if (variableList !== undefined) {
        for (const variable in variableList) {
            automation._processMessage(new Message(variable, variableList[variable], 'test'))
        }
    }
}

testrun.on('prepare', testcase => {
    const automation = new Automation({
        motionTopics: testcase.motionTopics,
        subscribeQoS: 2,
        longitude: 0.001545,
        latitude: 51.477928
    })
    automation.setRules(testcase.rule)
    return automation
})

const runTest = (test, automation) => {
    const testTime = timeOfDayStringToDate(test.time)
    sendMessages(automation, test.variables)
    setMotions(automation, test.motions)
    return {
        ...automation.processTasks(testTime),
        subscriptions: automation.getSubscriptions()
    }
}

testrun.on('run', runTest)

testrun.on('break', (test, automation) => {
    runTest(test, automation)
})

module.exports = () => { testrun.run(['leaving', 'duration'], __dirname, 13) }
