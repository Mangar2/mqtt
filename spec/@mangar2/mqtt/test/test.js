/**
 * ---------------------------------------------------------------------------------------------------
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * Author:      Volker Böhm
 * Copyright:   Volker Böhm
 * ---------------------------------------------------------------------------------------------------
 */
'use strict'

const Interface = require('@mangar2/mqtt')
const UnitTest = require('@mangar2/unittest')
const DEBUG = false
var mqtt = new Interface({ directory: '.', fileName: 'connections', timeoutInMilliseconds: 500 })
var unitTest = new UnitTest(DEBUG, DEBUG)

var packets = { publish: 0, pubrel: 0 }

mqtt.on('send', async (host, port, packet, payload, headers) => {
    const { message, token } = payload
    packets[packet]++
    unitTest.assertEqual(host, 'host0', 'host name')
    unitTest.assertEqual(port, 9001, 'port number')
    if (packet === 'publish') {
        const qos = Number(headers.qos)
        headers.packet = qos === 2 ? 'pubrec' : 'puback'
        unitTest.assertEqual(headers.qos, message.topic === '/b/2' ? 2 : 1, 'QoS')
        if (payload.message.reason[0].message === 'delete retain') {
            unitTest.assertEqual(message.value, '', 'message delete retain value')
        } else {
            unitTest.assertEqual(message.value, '1.0', 'message value')
        }
    } else {
        headers.packet = 'pubcomp'
    }
    unitTest.assertEqual(token, 'receive/a/b', 'token')
    return { statusCode: 204, headers, payload: {} }
})

async function processAllMessages (publish, pubrel) {
    packets = { publish: 0, pubrel: 0 }
    mqtt.processSendMessage()
    await unitTest.delay(1000)
    unitTest.assertEqual(packets.publish, publish, 'publish')
    unitTest.assertEqual(packets.pubrel, pubrel, 'pubrel')
    return packets
}

function setLog () {
    if (unitTest.verbose) {
        mqtt.processRequest('log', [{ topic: 'b/b', module: 'send' }, { topic: 'system/#', module: 'received' }, { topic: '/b/c', module: 'all' }])
    }
}

function testConnectClean () {
    const result = mqtt.processRequest('connect', { clientId: '/a/b', host: 'host0', port: '9001', clean: true }, { version: '1.0' })
    const token = JSON.parse(result.payload).token.send
    unitTest.assertEqual(result.statusCode, 200, 'http code ok')
    unitTest.assertEqual(JSON.parse(result.payload).present, 0, 'older session')
    return token
}

function testDisconnect () {
    const result = mqtt.processRequest('disconnect', { clientId: '/a/b' }, { version: '1.0' })
    unitTest.assertEqual(result.statusCode, 204, 'disconnect, http code ok')
}

function testSubscribeNotConnected () {
    const result = mqtt.processRequest('subscribe',
        { clientId: '/a/b', topics: { topic1: 1, topic2: 0, topic3: 2 } },
        { packetid: 1, version: '1.0' }
    )
    unitTest.assertEqual(typeof (result.payload), 'string', 'subscribe without connected 1')
    unitTest.assertEqual(JSON.parse(result.payload).qos[0], 0x80, 'subscribe without connected 2')
}

function testSubscribeUnsubscribeNewFormat () {
    const result = mqtt.processRequest('subscribe',
        { clientId: '/a/b', topics: { topic1: 1, topic2: 0, topic3: 2 } },
        { packetid: 1, version: '1.0' }
    )
    unitTest.assertEqual(JSON.parse(result.payload).qos[0], 1, 'subscribe 1')
    mqtt.processRequest('unsubscribe', { clientId: '/a/b', topics: ['topic1', 'topic2', 'topic3'] }, { version: '1.0' })
}

function testSubscribeOldFormat () {
    const result = mqtt.processRequest('subscribe',
        { clientId: '/a/b', subscribe: { QoS: 1, topics: ['topic1', 'topic2', 'topic32'] } },
        { packetid: 2, version: '1.0' }
    )
    unitTest.assertEqual(JSON.parse(result.payload).qos[1], 1, 'subscribe 2')
}

async function subscribe () {
    const result = mqtt.processRequest('subscribe',
        { clientId: '/a/b', topics: { '/b/#': 1, '/b/2': 2 } },
        { packetid: 3, version: '1.0' }
    )
    unitTest.assertEqual(JSON.parse(result.payload).qos[0], 1, 'subscribe 3')
    return result
}

/**
 * Test retain messages
 * @param {string} token send token
 */
async function testRetain (token) {
    unitTest.log('test retain')
    // clear retain
    let result = mqtt.processRequest('publish',
        { token, message: { topic: '/b/c', reason: 'delete retain' } },
        { packetid: '10', qos: '1', retain: 1, version: '1.0' }
    )
    unitTest.assertEqual(Number(result.headers.packetid), 10, 'retain 1')
    // qos = 1
    result = mqtt.processRequest('publish',
        { token, message: { topic: '/b/c', value: '1.0', reason: 'test' } },
        { packetid: '11', qos: '1', dup: '1', retain: 1, version: '1.0' }
    )
    unitTest.assertEqual(Number(result.headers.packetid), 11, 'retain 2')
    // clear retain
    result = mqtt.processRequest('publish',
        { token, message: { topic: '/b/c', reason: 'delete retain' } },
        { packetid: '12', qos: '1', retain: 1, version: '1.0' }
    )
    unitTest.assertEqual(Number(result.headers.packetid), 12, 'retain 3')
    // messages of three topics
    await processAllMessages(1, 0)
    await processAllMessages(1, 0)
    await processAllMessages(1, 0)
    await processAllMessages(0, 0)
}

/**
 * tests pingreq
 */
async function testPingreq () {
    unitTest.log('test pingreq')
    const token = testConnectClean()
    let result = mqtt.processRequest('pingreq', { token })
    unitTest.assertEqual(result.headers.packet, 'pingresp', 'ping 1')
    unitTest.assertEqual(result.statusCode, 204, 'ping 2')
    testDisconnect()
    result = mqtt.processRequest('pingreq', { token })
    unitTest.assertEqual(result.statusCode, 400, 'ping 3')
}

/**
 * Publishes three messages
 */
async function testPublish (token) {
    if (unitTest.verbose) {
        console.log('test publish')
    }
    let result = mqtt.processRequest('publish',
        { token, message: { topic: '/b/c', value: '1.0', reason: 'test' } },
        { packetid: '0', qos: '1', version: '1.0' }
    )
    unitTest.assertEqual(Number(result.headers.packetid), 0, 'publish 1')
    // qos = 1
    result = mqtt.processRequest('publish',
        { token, message: { topic: '/b/b', value: '1.0', reason: 'test' } },
        { packetid: '1', qos: '1', dup: '1', version: '1.0' }
    )
    unitTest.assertEqual(Number(result.headers.packetid), 1, 'publish 2')
    // qos = 2
    result = mqtt.processRequest('publish',
        { token, message: { topic: '/b/2', value: '1.0', reason: 'test' } },
        { packetid: '2', qos: '2', dup: '0', version: '1.0' }
    )
    unitTest.assertEqual(Number(result.headers.packetid), 2, 'publish 3')
    // qos = 2, dup = 1, may not send a second message!
    mqtt.processRequest('publish',
        { token, message: { topic: '/b/2', value: '1.0', reason: 'test' } },
        { packetid: '2', qos: '2', dup: '1', version: '1.0' }
    )
    // Send the pubrel to free the packetid
    mqtt.processRequest('pubrel', { token }, { packetid: '2', version: '1.0' })
    // qos = 2, dup = 1, now send it
    mqtt.processRequest('publish',
        { token, message: { topic: '/b/2', value: '1.0', reason: 'publish 4' } },
        { packetid: '2', qos: '2', dup: '1', version: '1.0' }
    )
}

(async function testInterface () {
    setLog()
    await testPingreq()
    testConnectClean()
    testDisconnect()
    testSubscribeNotConnected()

    const token = testConnectClean()
    testSubscribeUnsubscribeNewFormat()
    testSubscribeOldFormat()

    await processAllMessages(0, 0)
    subscribe()
    await testRetain(token)
    await testPublish(token)

    // messages of three topics
    await processAllMessages(3, 0)
    // One message has qos2, pubrel ...
    await processAllMessages(0, 1)
    // One message is a second message to a topic after a pubrel
    await processAllMessages(1, 0)
    // One message has qos2, pubrel ...
    await processAllMessages(0, 1)
    await processAllMessages(0, 0)

    unitTest.showResult(83)
})().catch((result) => {
    console.log(result)
    console.log('fail')
})
