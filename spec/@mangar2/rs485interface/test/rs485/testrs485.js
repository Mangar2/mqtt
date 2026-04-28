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

const TestRun = require('@mangar2/testrun')
const Schedule = require('../../rs485schedule')
const SerialMessage = require('../../serialmessage')
const { types } = require('@mangar2/utils')
const { TOKEN_COMMAND, ENABLE_SEND, REGISTRATION_REQUEST, REGISTRATION_INFO } = require('../../constants')

const VERBOSE = false
const PARALLEL = true
const testrun = new TestRun(VERBOSE, PARALLEL)

/**
 * Creates a test message based on a defintion
 * @param {[type, sender, receiver]} definition message definition
 * @returns {SerialMessage|undefined} Created message
 */
const createTestMessage = (definition) => {
    let message
    if (definition) {
        message = new SerialMessage(1)
        message.sender = definition[1]
        message.receiver = definition[2]
        if (types.isBoolean(definition[3])) {
            message.reply = definition[3]
        }
        if (definition[0] === 'enableSend') {
            message.command = TOKEN_COMMAND
            message.value = ENABLE_SEND
        } else {
            message.command = definition[0]
            message.value = 2
        }
    }
    return message
}

const messages = {
    enableSend: [0x01, 0x00, 0x02, 0x09, 0x21, 0x00, 0x01],
    registrationRequest: [0x01, 0x00, 0x02, 0x09, 0x21, 0x00, 0x03]
}

const createExpectedData = (expected) => {
    let message
    if (types.isString(expected)) {
        message = messages[expected]
    } else {
        message = []
        message[0] = expected.sender
        message[1] = expected.receiver
        message[2] = expected.reply ? 0x03 : 0x02
        message[3] = 0x09
        message[4] = expected.command === 'enableSend' ? 0x21 : expected.command.charCodeAt(0)
        message[5] = 0x00
        switch (expected.command) {
        case 'enableSend': message[6] = ENABLE_SEND; break
        case 'registrationRequest': message[6] = REGISTRATION_REQUEST; break
        case 'registrationInfo': message[6] = REGISTRATION_INFO; break
        default: message[6] = 2
        }
    }
    return message
}

testrun.on('prepare', async testcase => {
    const schedule = new Schedule({ myAddress: 1, maxVersion: 1, tickDelay: 10 })
    schedule.run()
    return schedule
})

testrun.on('followup', async (schedule) => {
    await schedule.close()
})

const runTest = async (test, schedule) => {
    const resultMessages = []
    const messageAmount = test.messageAmount | 1
    const receivedMessage = createTestMessage(test.receive)
    if (receivedMessage) {
        schedule.processReceivedMessage(receivedMessage)
    }
    const sendMessage = createTestMessage(test.send)
    if (sendMessage) {
        schedule.sendMessage(sendMessage)
    }
    const result = new Promise((resolve, reject) => {
        schedule.on('send', async resultMessage => {
            resultMessages.push(resultMessage)
            if (resultMessages.length === messageAmount) {
                resolve(resultMessages)
            }
        })
    })
    return result
}

testrun.on('run', runTest)

testrun.on('break', (test, schedule) => {
    runTest(test, schedule)
})

testrun.on('validate', async (test, result, path) => {
    let validate = true
    for (const messageIndex in test.expected) {
        const expectedMessage = createExpectedData(test.expected[messageIndex])
        const resultMessage = result[messageIndex]
        let isEqual = true
        let index
        for (index in expectedMessage) {
            if (expectedMessage[index] !== resultMessage[index]) {
                isEqual = false
                break
            }
        }
        testrun.unitTest.assertTrue(isEqual, path + '/' + JSON.stringify(expectedMessage))
        validate = validate && isEqual
    }
    if (!validate) {
        console.log(result)
        await testrun.asyncRunAgain()
    }
})

const testRunCommand = async () => {
    await testrun.asyncRun(
        [
            'tokenhandling',
            'queuemessage'
        ],
        __dirname,
        68
    )
}

testRunCommand()
