/* eslint-disable no-unmodified-loop-condition */
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
const TestRun = require('@mangar2/testrun')
const RunCommand = require('../runcommand.js')
const Message = require('@mangar2/message')

const testRun = new TestRun(VERBOSE)
let testCount = 0
let testDone = 0

testRun.on('prepare', async (testCase) => {
    while (testCount > 0) {
        await testRun.unitTest.delay(100)
    }
    const runCommand = new RunCommand(testCase.options)
    return runCommand
})

testRun.on('break', (test, prepared) => {
})

testRun.on('run', async (test, runCommandPromise) => {
    while (testCount > 0) {
        await testRun.unitTest.delay(100)
    }
    testCount++
    const runCommand = await runCommandPromise
    const message = new Message('test', test.commandName, test.description)
    const result = new Promise((resolve) => {
        const returnMessages = []
        runCommand.on('publish', (returnMessage) => {
            returnMessages.push(returnMessage)
            if (returnMessages.length === 2) {
                resolve(returnMessages)
            }
        })
        runCommand.processMessage(message)
    })
    return result
})

testRun.on('validate', async (test, result, path) => {
    try {
        const messages = await result
        let isEqual = true
        for (const index in test.expected) {
            const expectedMessage = test.expected[index]
            const message = messages[index]
            isEqual = isEqual && testRun.unitTest.assertEqual(message.topic, expectedMessage.topic, path)
            isEqual = isEqual && testRun.unitTest.assertEqual(message.value, expectedMessage.value, path)
            isEqual = isEqual && testRun.unitTest.assertEqual(message.reason[1].message, expectedMessage.message, path)
        }
        if (!isEqual) {
            console.log(JSON.stringify(messages, null, 2))
        }
    } catch (err) {
        console.log(err)
    }
    testCount--
    testDone++
})

async function testRunCommand () {
    testRun.run([
        'commands'
    ], __dirname)

    while (testDone < testRun.testNo) {
        await testRun.unitTest.delay(200)
    }
    testRun.unitTest.showResult(24)
}

testRunCommand()
