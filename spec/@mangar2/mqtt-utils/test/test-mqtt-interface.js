/**
 * ---------------------------------------------------------------------------------------------------
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * File:      mqttversion.js
 *
 * Author:      Volker Böhm
 * Copyright:   Volker Böhm
 * ---------------------------------------------------------------------------------------------------
 */

'use strict'

const { Interfaces } = require('../dist/index')
const { UnitTest } = require('@mangar2/unittest')

const DEBUG = true

module.exports = () => {
    var unitTest = new UnitTest(DEBUG)
    const message = { topic: 'a/b', value: 1, reason: [{ message: 'test' }], qos: 1 }
    const TOKEN = 'token'

    let publish = Interfaces.publish('0.0', { TOKEN, message, dup: false, packetid: 1 })
    let onPublish = Interfaces.onPublish(publish.headers)
    unitTest.assertNoException(() => publish.resultCheck(onPublish), 'publish ver. 0.0')

    publish = Interfaces.publish('1.0', { TOKEN, message, dup: false, packetid: 1 })
    onPublish = Interfaces.onPublish(publish.headers)
    unitTest.assertNoException(() => publish.resultCheck(onPublish), 'publish ver. 1.0')

    let pubrel = Interfaces.pubrel('0.0', TOKEN, 1)
    let onPubrel = Interfaces.onPubrel(pubrel.headers)
    unitTest.assertNoException(() => pubrel.resultCheck(onPubrel), 'pubrel ver. 0.0')

    pubrel = Interfaces.pubrel('1.0', TOKEN, 1)
    onPubrel = Interfaces.onPubrel(pubrel.headers)
    unitTest.assertNoException(() => pubrel.resultCheck(onPubrel), 'pubrel ver. 1.0')

    let subscribe = Interfaces.subscribe('0.0', { 'a/b': 1 }, 'client/test', 2)
    let onSubscribe = Interfaces.onSubscribe(subscribe.headers, [1])
    unitTest.assertNoException(() => subscribe.resultCheck(onSubscribe), 'subscribe ver. 0.0')

    subscribe = Interfaces.subscribe('1.0', { 'a/b': 1 }, 'client/test', 2)
    onSubscribe = Interfaces.onSubscribe(subscribe.headers, { qos: [1] })
    unitTest.assertNoException(() => subscribe.resultCheck(onSubscribe), 'subscribe ver. 1.0')

    let unsubscribe = Interfaces.unsubscribe('0.0', ['a/b'], 'client/test', 3)
    let onUnsubscribe = Interfaces.onUnsubscribe(unsubscribe.headers, [1])
    unitTest.assertNoException(() => unsubscribe.resultCheck(onUnsubscribe), 'unsubscribe ver. 0.0')

    unsubscribe = Interfaces.unsubscribe('1.0', ['a/b'], 'client/test', 3)
    onUnsubscribe = Interfaces.onUnsubscribe(unsubscribe.headers, [0])
    unitTest.assertNoException(() => unsubscribe.resultCheck(onUnsubscribe), 'unsubscribe ver. 1.0')

    let connect = Interfaces.connect('0.0', { clientId: 'client/test', host: 'host', port: 9000, clean: true })
    let onConnect = Interfaces.onConnect(connect.headers, [1])
    unitTest.assertNoException(() => connect.resultCheck(onConnect), 'connect ver. 0.0')

    connect = Interfaces.connect('1.0', { clientId: 'client/test', host: 'host', port: 9000, clean: true, keepAlive: 3600000 })
    onConnect = Interfaces.onConnect(connect.headers, { mqttcode:0, present: 0, token: { send: 'abc', receive: 'def'}})
    unitTest.assertNoException(() => connect.resultCheck(onConnect), 'connect ver. 1.0')

    let disconnect = Interfaces.disconnect('0.0', 'client/test')
    let onDisconnect = Interfaces.onDisconnect(disconnect.headers, [1])
    unitTest.assertNoException(() => disconnect.resultCheck(onDisconnect), 'disconnect ver. 0.0')

    disconnect = Interfaces.disconnect('1.0', 'client/test')
    onDisconnect = Interfaces.onDisconnect(disconnect.headers)
    unitTest.assertNoException(() => disconnect.resultCheck(onDisconnect), 'disconnect ver. 1.0')

    return unitTest.getResultFunctions(12)
}
