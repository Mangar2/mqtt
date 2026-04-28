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

const Receive = require('@mangar2/mqttservice').OnPublish
const Publish = require('@mangar2/mqttservice').Publish
const UnitTest = require('@mangar2/unittest')
const Message = require('@mangar2/message')
const Connect = require('@mangar2/connect')
const { shutdown } = require('@mangar2/utils')
const Broker = require('@mangar2/broker')

const VERBOSE = true
const DEBUG = true

var unitTest = new UnitTest(VERBOSE, DEBUG)

const HOST = '127.0.0.1'
const PORT = 9001
const MY_PORT = 9002
const LAST = 100

const DELAY = 5000
const BROKER_TIMEOUT_IN_MILLISECONDS = 10000
const BROKER_RETRY_COUNT = 10

const config = {
    port: PORT,
    persistInterval: 0,
    connections: {
        fileName: 'test.json',
        directory: '.'
    }
}

const broker = new Broker(config)
const connect = new Connect('broker/test/client', HOST, PORT, MY_PORT)
const publish = new Publish(HOST, PORT, { retry: 10 })
const receive = new Receive(MY_PORT)

let receivedTopics = {}

receive.on('publish', (message, qos, dup) => {
    const index = message.topic
    if (receivedTopics[index] === undefined) {
        receivedTopics[index] = { amount: 0, value: 0, dup: 0, error: 0 }
    }
    receivedTopics[index].amount++
    if (dup && message.value === receivedTopics[index].value) {
        receivedTopics[index].dup++
    } else if (message.value !== receivedTopics[index].value + 1) {
        if (unitTest.verbose) {
            console.log('%s, value:%s, dup:%s old:%s', message.topic, message.value, dup, receivedTopics[index].value)
        }
        receivedTopics[index].error++
    }
    if (message.value > receivedTopics[index].value) {
        receivedTopics[index].value = message.value
    }
})

async function connectToBroker (clean) {
    const topics = {
        '/a/0/+': 0,
        '/a/1/+': 1,
        '/a/2/+': 2,
        '/b/#': 1,
        '/c/+/1234/+/#': 1
    }
    const result = await connect.connectAndSubscribe(clean, topics)
    unitTest.assertEqual(JSON.stringify(result.qos), '[0,1,2,1,1]', 'connect')
    return result
}

async function disconnectBroker () {
    const result = await connect.disconnect()
    unitTest.assertEqual(JSON.stringify(result), '{}', 'disconnect')
}

/**
 * Test an invalid connection
 */
async function testInvalidConnection () {
    const notConnect = new Connect('hello', HOST, PORT, 'morning')
    await notConnect.connect(true).catch(() => { })
    unitTest.assertTrue(!notConnect.isConnected, 'Connect failed safely')
}

/**
 * Test receive client info
 */
async function testGetClient () {
    await connectToBroker(true)
    const clientInfo = broker._getClientInfo('host/clients/broker/test/client')
    const client = clientInfo.payload
    unitTest.assertEqual(client._clientId, 'broker/test/client', 'check getClientInfo')
    await disconnectBroker()
}

/**
 * Test sending several messages connected
 */
async function testSendingSingleMessages () {
    receivedTopics = {}
    const result = await connectToBroker(true)
    const sendToken = result.token.send

    // Publish single qos=0 message
    publish.publish(sendToken, new Message('/a/0/test1', 1, 'test 1'), 0, false)
    await unitTest.delay(100)
    unitTest.assertEqual(receivedTopics['/a/0/test1'].amount, 1, 'sending data 0')
    receivedTopics = {}
    // Publish single qos=1 message
    publish.publish(sendToken, new Message('/a/1/test1', 1, 'test 1'), 1, false)
    await unitTest.delay(100)
    unitTest.assertEqual(receivedTopics['/a/1/test1'].amount, 1, 'sending data 1')
    receivedTopics = {}
    // Publish single qos=2 message
    publish.publish(sendToken, new Message('/a/2/test1', 1, 'test 1'), 2, false)
    await unitTest.delay(1000)
    unitTest.assertEqual(receivedTopics['/a/2/test1'].amount, 1, 'sending data 2')
}

async function testSendingManyMessages (loopCount) {
    receivedTopics = {}
    const sendToken = (await connectToBroker(true)).token.send

    for (let i = 1; i <= loopCount; i++) {
        publish.publish(sendToken, new Message('/a/0/test1', i, 'test 1'), 0, false)
        publish.publish(sendToken, new Message('/a/1/test2', i, 'test 2'), 1, false)
        publish.publish(sendToken, new Message('/a/2/test2', i, 'test 2'), 2, false)
        publish.publish(sendToken, new Message('/b/2/test2', i, 'test 2'), 1, false)
        publish.publish(sendToken, new Message('/c/2/1234/z/hello/world', i, 'test 2'), 1, false)
        publish.publish(sendToken, new Message('/d/2/1234/z/hello/world', i, 'test 2'), 1, false)
    }
    await unitTest.delay(DELAY)
    unitTest.assertEqual(receivedTopics['/a/0/test1'].amount, LAST, 'sending data 1')
    unitTest.assertEqual(receivedTopics['/a/1/test2'].amount, LAST, 'sending data 2')
    unitTest.assertEqual(receivedTopics['/a/2/test2'].amount, LAST, 'sending data 3')
    unitTest.assertEqual(receivedTopics['/b/2/test2'].amount, LAST, 'sending data 4')
    unitTest.assertEqual(receivedTopics['/c/2/1234/z/hello/world'].amount, LAST, 'sending data 5')
    unitTest.assertEqual(receivedTopics['/d/2/1234/z/hello/world'], undefined, 'sending data 6')
    unitTest.assertEqual(receivedTopics['/a/1/test2'].error, 0, 'sending data 7')
    unitTest.assertEqual(receivedTopics['/a/2/test2'].error, 0, 'sending data 8')
    unitTest.assertEqual(receivedTopics['/b/2/test2'].error, 0, 'sending data 9')
    unitTest.assertEqual(receivedTopics['/c/2/1234/z/hello/world'].error, 0, 'sending data 10')
}

/**
 * Test retain message
 */
async function testRetain () {
    receivedTopics = {}
    let sendToken = (await connectToBroker(true)).token.send

    for (let i = 1; i <= 2; i++) {
        publish.publish(sendToken, new Message('/a/0/retain', i, 'test 1'), 0, true)
        publish.publish(sendToken, new Message('/a/1/retain', i, 'test 2'), 1, true)
        publish.publish(sendToken, new Message('/a/2/retain', i, 'test 2'), 1, false)
        publish.publish(sendToken, new Message('/b/2/retain', i, 'test 2'), 1, true)
    }
    await unitTest.delay(1000)
    unitTest.assertEqual(receivedTopics['/a/0/retain'].amount, 2, 'retain 1')
    unitTest.assertEqual(receivedTopics['/a/1/retain'].amount, 2, 'retain 2')
    unitTest.assertEqual(receivedTopics['/a/2/retain'].amount, 2, 'retain 3')
    unitTest.assertEqual(receivedTopics['/b/2/retain'].amount, 2, 'retain 4')
    unitTest.assertEqual(receivedTopics['/a/0/retain'].error, 0, 'retain 5')
    unitTest.assertEqual(receivedTopics['/a/1/retain'].error, 0, 'retain 6')
    unitTest.assertEqual(receivedTopics['/a/2/retain'].error, 0, 'retain 7')
    unitTest.assertEqual(receivedTopics['/b/2/retain'].error, 0, 'retain 8')

    // Retained messages sent on reconnect - even, if the client is clean
    receivedTopics = {}
    await disconnectBroker()
    sendToken = (await connectToBroker(true)).token.send
    await unitTest.delay(1000)

    unitTest.assertEqual(receivedTopics['/a/0/retain'].amount, 1, 'retain 9')
    unitTest.assertEqual(receivedTopics['/a/1/retain'].amount, 1, 'retain 10')
    unitTest.assertEqual(receivedTopics['/a/2/retain'], undefined, 'retain 11')
    unitTest.assertEqual(receivedTopics['/b/2/retain'].amount, 1, 'retain 12')
    unitTest.assertEqual(receivedTopics['/a/0/retain'].value, 2, 'retain 13')
    unitTest.assertEqual(receivedTopics['/a/1/retain'].value, 2, 'retain 14')
    unitTest.assertEqual(receivedTopics['/b/2/retain'].value, 2, 'retain 15')

    // Delete retain messages by send empty string as value
    // Case 1: empty string
    publish.publish(sendToken, { topic: '/a/0/retain', value: '' }, 0, true)
    // Case 2: non empty string
    publish.publish(sendToken, { topic: '/a/1/retain', value: 1 }, 1, true)
    // Case 3: does not have a retain message
    publish.publish(sendToken, { topic: '/a/2/retain', value: '' }, 1, true)
    // Case 4: fully scip the value
    publish.publish(sendToken, { topic: '/b/2/retain' }, 1, true)
    receivedTopics = {}
    await disconnectBroker()
    sendToken = (await connectToBroker(true)).token.send
    await unitTest.delay(1000)

    unitTest.assertEqual(receivedTopics['/a/0/retain'], undefined, 'retain 16')
    unitTest.assertEqual(receivedTopics['/a/1/retain'].amount, 1, 'retain 17')
    unitTest.assertEqual(receivedTopics['/a/2/retain'], undefined, 'retain 18')
    unitTest.assertEqual(receivedTopics['/b/2/retain'], undefined, 'retain 19')
    publish.publish(sendToken, { topic: '/a/1/retain', value: '' }, 1, true)
}

/**
 * Test sending several messages disconnected
 */
async function testSendOnDisconnect () {
    receivedTopics = {}
    const sendToken = (await connectToBroker(false)).token.send
    await disconnectBroker()
    for (let i = 1; i <= LAST; i++) {
        publish.publish(sendToken, new Message('/a/0/disconnect', i, 'test 1'), 0, false)
        publish.publish(sendToken, new Message('/a/1/disconnect', i, 'test 2'), 1, false)
    }
    await unitTest.delay(1000)
    await connectToBroker(false)
    await unitTest.delay(DELAY)
    unitTest.assertEqual(receivedTopics['/a/0/disconnect'], undefined, 'sending disconnected 1')
    unitTest.assertEqual(receivedTopics['/a/1/disconnect'].amount, LAST, 'sending disconnected 2')
    unitTest.assertEqual(receivedTopics['/a/1/disconnect'].error, 0, 'sending disconnected 3')
}

/**
 * Test sending without listener
 */
async function testSendNoListener () {
    receivedTopics = {}
    const sendToken = (await connectToBroker(false)).token.send
    await receive.close()
    for (let i = 1; i <= LAST; i++) {
        publish.publish(sendToken, new Message('/a/0/close', i, 'test 1'), 0, false)
        publish.publish(sendToken, new Message('/a/1/close', i, 'test 2'), 1, false)
    }
    await unitTest.delay(BROKER_TIMEOUT_IN_MILLISECONDS * 1)
    await receive.listen()
    await unitTest.delay(BROKER_TIMEOUT_IN_MILLISECONDS * 2)
    unitTest.assertEqual(receivedTopics['/a/0/close'], undefined, 'sending closed 1')
    unitTest.assertEqual(receivedTopics['/a/1/close'].amount, LAST, 'sending closed 2')
    unitTest.assertEqual(receivedTopics['/a/1/close'].error, 0, 'sending closed 3')
}

/**
  * Test auto disconnect after timeout
  */
async function testAutoDisconnect () {
    receivedTopics = {}
    const sendToken = (await connectToBroker(false)).token.send
    await receive.close()
    for (let i = 1; i <= LAST; i++) {
        publish.publish(sendToken, new Message('/a/1/auto disconnect', i, 'test 2'), 1, false)
    }
    // Wait until the broker actively disconnects this client
    await unitTest.delay(BROKER_TIMEOUT_IN_MILLISECONDS * (BROKER_RETRY_COUNT + 2))
    // The client is disconnected!
    await receive.listen()
    await unitTest.delay(BROKER_TIMEOUT_IN_MILLISECONDS * 2)
    unitTest.assertEqual(receivedTopics['/a/1/auto disconnect'], undefined, 'auto disconnect 1 - no data received')
    await connectToBroker(false)
    await unitTest.delay(BROKER_TIMEOUT_IN_MILLISECONDS * 2)
    unitTest.assertEqual(receivedTopics['/a/1/auto disconnect'].amount, LAST, 'auto disconnect 2 - received all messages after reconnect')
    unitTest.assertEqual(receivedTopics['/a/1/auto disconnect'].error, 0, 'auto disconnect 3 - check errors')
}

(async function test () {
    try {
        console.log('this test will run several minutes, please wait ...')
        broker.run()
        await receive.listen()
        await testGetClient()
        console.log('Testing invalid Connection... the error message port is not a number is intended')
        await testInvalidConnection()
        await testSendingSingleMessages()
        await testSendingManyMessages(LAST)
        await testRetain()
        await testSendOnDisconnect()
        await testSendNoListener()
        await testAutoDisconnect()

        unitTest.showResult(58)
        process.exit(0)
    } catch (err) {
        console.error(err)
        console.error('Error: test ends with errors')
        process.exit(1)
    }
})()

shutdown(async () => {
    await disconnectBroker()
    await receive.close()
    console.log(receivedTopics)
    process.exit(0)
})
