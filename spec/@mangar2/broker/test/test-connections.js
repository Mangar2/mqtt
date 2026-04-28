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
const { delay } = require('@mangar2/utils')
const { Message } = require('@mangar2/mqtt-utils')
const { TestRun } = require('@mangar2/unittest')
const { Connections } = require('@mangar2/broker/dist/connections/connections')
const VERBOSE = false
const testrun = new TestRun(VERBOSE)

const config = {
    fileName: 'connectionData',
    directory: '.',
    replyTimeoutInMilliseconds: 10,
    pubrelTimeoutInMilliseconds: 1000,
    maxRetryCount: 10
}

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

testrun.on('prepare', async testcase => {
    if (testcase.inFlightWindow) {
        config.inFlightWindow = testcase.inFlightWindow
    }
    const connections = new Connections(config)
    connect(connections, testcase.connect)
    subscribe(connections, testcase.subscribe)

    return connections
})

function getSendToken(connections, messageQueueEntry) {
    const clientId = messageQueueEntry.clientId
    const client = connections.getClientById(clientId)
    const sendToken = client.sendToken
    return sendToken
}

/**
 * Retrieves a list of messages and acknowledges it
 * @param {Connections} connections 
 * @returns {Array} List of messages
 */
function getAndAcknowledgeMessageList(connections) {
    const messageList =  connections.getAllMessagesToSend()
    const copiedList = []
    for (const messageQueueEntry of messageList) {
        copiedList.push({...messageQueueEntry})
        connections.acknowledgeMessage(messageQueueEntry)
        if (messageQueueEntry.status === 'pubrel') {
            const sendToken = getSendToken(connections, messageQueueEntry)
            connections.onPubrel(sendToken, messageQueueEntry.packetid)
        }
    }
    return copiedList
}

const runTest = async (test, connections) => {
    // Test the set from JSON functionality
    // const connections = new Connections(config)
    // connections.setFromJSON(JSON.parse(connectionsJSON))

    connect(connections, test.connect)
    subscribe(connections, test.subscribe)
    if (test.publish) {
        for (const publish of test.publish) {
            const message = new Message(publish.topic, publish.value, publish.reason, publish.qos, publish.retain)
            connections.publishMessage(message)
        }
    }
    if (test.disconnect) {
        for (const clientId of test.disconnect) {
            connections.disconnect(clientId)
        }
    }
    if (test.unsubscribe) {
        for (const unsubscribe of test.unsubscribe) {
            connections.unsubscribe(unsubscribe.clientId, unsubscribe.topics)
        }
    }
    if (test.delay) {
        await delay(test.delay)
        connections.cleanup()
    }
    let messageLists = []
    if (test.acknowledge === false) {
        const messageList =  connections.getAllMessagesToSend()
        if (messageList.length > 0) {
            messageLists.push(messageList)
        }
    } else {
        let messageList =  getAndAcknowledgeMessageList(connections)
    
        while (messageList.length > 0) {
            messageLists.push(messageList)
            messageList =  getAndAcknowledgeMessageList(connections)
        }
    }
    return messageLists
}

testrun.on('run', runTest)

testrun.on('break', async (test, automation) => {
    runTest(test, automation)
})


module.exports = async () => {
    return await testrun.asyncRun([
        'test-publish-cases',
        'test-disconnect-cases',
        'test-unsubscribe-cases',
        'test-will-cases',
        'test-clean-cases',
        'test-inflight-cases',
        'test-no-acknowledge-cases'
    ], __dirname, 59, 'js')
}