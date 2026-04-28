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

const Message = require('@mangar2/message')
const { Automation } = require('@mangar2/automation')
const Testrun = require('@mangar2/testrun')

const VERBOSE = false
const testrun = new Testrun(VERBOSE)

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
    for (const topic in test.message) {
        const value = test.message[topic]
        automation._processMessage(new Message(topic, value, topic))
    }
    return automation.processTasks(new Date())
}

testrun.on('run', runTest)

testrun.on('break', (test, parser) => {
    runTest(test, parser)
})

module.exports = () => {
    console.log('running makro test')

    testrun.run(
        [
            'makros'
        ],
        __dirname,
        2
    )
}
