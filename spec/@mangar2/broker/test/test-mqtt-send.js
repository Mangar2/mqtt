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
//const { delay } = require('@mangar2/utils')
const { Message } = require('@mangar2/mqtt-utils')
const { TestRun } = require('@mangar2/unittest')
const { MqttSend } = require('../dist/mqtt/mqtt-send')
const VERBOSE = true
const testrun = new TestRun(VERBOSE)

function connect(connections, connectList) {
    if (connectList) {
        for (const connection of connectList) {
            connections.connect(connection.clientId, connection.host, connection.port, connection.clean, connection.version, connection.keepAlive)
            if (connection.will) {
                connections.setWill(connection.clientId, connection.will)
            }
        }
    }
}

function subscribe(connections, subscribeList) {
    if (subscribeList) {
        for (const subscription of subscribeList) {
            connections.subscribe(subscription.client, subscription.topic)
        }
    }
}

function publish(connections, publishList) {
    if (publishList) {
        for (const publish of publishList) {
            const message = new Message(publish.topic, publish.value, publish.reason, publish.qos, publish.retain)
            connections.publishMessage(message)
        }
    }
}

testrun.on('prepare', async testcase => {
    const mqttSend = new MqttSend(testcase.configuration)
    connect(mqttSend.connections, testcase.connect)
    subscribe(mqttSend.connections, testcase.subscribe)
    return mqttSend
})

const runTest = async (test, mqttSend) => {
    publish(mqttSend.connections, test.publish)
    const sendMessages = []
    mqttSend.on('send', (host, port, command, payload, headers) => sendMessages.push({ host, port, command, payload, headers }))
    mqttSend.sendMessages()
    return sendMessages
}

testrun.on('run', runTest)

testrun.on('break', async (test, automation) => {
    runTest(test, automation)
})


module.exports = async () => {
    return await testrun.asyncRun([
        'test-mqtt-send-cases'
    ], __dirname, 59, 'js')
}