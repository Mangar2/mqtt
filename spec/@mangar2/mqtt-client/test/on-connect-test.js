/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Provides a standard client to communicate with the mqtt broker
 */

'use strict'

const { TestRun } = require('@mangar2/unittest')
const { Connect, OnConnect} = require('@mangar2/mqtt-client/dist') 

const VERBOSE = true
const PARALLEL = false

const testRun = new TestRun(VERBOSE, PARALLEL)

testRun.on('prepare', async (testSet) => {
    const { result } = testSet
    const connect = new Connect(testSet.clientId)
    class Mqtt {
        constructor() {
            this.history = []
        }
        connect(options) { this.history.push(options); return result }
        disconnect(options) { this.history.push(options)}
        subscribe(options) { this.history.push(options); return result }
        unsubscribe(options) { this.history.push(options); return result }
    }
    const mqtt = new Mqtt
    const onConnect = new OnConnect(mqtt)

    return { connect, onConnect, mqtt }
    
})

const runTest = async (testCase, testObject) => {
    const { method, args } = testCase
    const { connect, onConnect, mqtt } = testObject
    let result
    mqtt.history = []
    connect.on('send', (method, path, headers, payload) => { 
        return onConnect.handleHttpRequest(path, headers, JSON.stringify(payload))
    })
    try {
        result = await connect[method](...args)
    } catch (error) {
        // errorLog(error, DEBUG)
        result = error.message
    }

    return { result, history: mqtt.history }
}

testRun.on('run', async (testCase, testObject) => {
    return runTest(testCase, testObject)
})

testRun.on('break', async (testCase, testObject) => {
    // Re-run the test for debugging purposes
    const result = await runTest(testCase, testObject)
    return result
})

testRun.on('cleanup', async () => {
    // Implement any necessary cleanup, e.g., closing connections
})

module.exports = () => testRun.asyncRun( 
    [
        'on-connect-cases',
        'on-subscribe-cases'
    ], 
    __dirname, 7, 'js' )
