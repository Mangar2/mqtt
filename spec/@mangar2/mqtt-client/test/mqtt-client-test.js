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
const { Message, Logger } = require('@mangar2/mqtt-utils')

const { MqttClient, HttpReceiveServices } = require('@mangar2/mqtt-client/dist')


const VERBOSE = true
const PARALLEL = false

const testRun = new TestRun(VERBOSE, PARALLEL)

class Mqtt {
    constructor() {
        this.history = []
    }
    connect(options) { 
        options.method = 'connect'
        this.history.push(options)  
        return { clean: false, token: { send:'abcd', receive: 'efgh' }, present: 0, mqttcode: 0 }
    }
    disconnect(options) { 
        options.method = 'disconnect'
        this.history.push(options) 
    }
    subscribe(options) { 
        options.method = 'subscribe'
        this.history.push(options) 
        const result = []
        for (const topic in options.topics) {
            result.push(options.topics[topic])
        }   
        return result
    }
    unsubscribe(options) { 
        options.method = 'unsubscribe'
        this.history.push(options) 
    }
    pingreq(options) { 
        this.history.push({ method: 'pingreq', token: options}) 
    }
    publish(options) { 
        options.method = 'publish'
        this.history.push(options) 
    }
}

testRun.on('prepare', async (/*testSet*/) => {
    const logger = new Logger()
    const receive = new HttpReceiveServices(0, logger)
    const mqtt = new Mqtt()
    receive.setMqttServer(mqtt)
    receive.listen()
    const port = receive.port
    const client = new MqttClient({ clientId: 'test', brokerOptions: { host: 'localhost', port} })
    return { receive, client, mqtt }
})

const runTest = async (testCase, testObject) => {
    let { client, mqtt } = testObject
    const { method, message, args } = testCase
    let result
    try {
        mqtt.history = []
        if (message) {
            result = await client[method](new Message(...message))
        } else if (args) {
            if (method === 'subscribe') {
                result = await client.registerRecipient(...args, (message) => { return message })
            }
        } else {
            result = await client[method]()
        }
    } catch (error) {
        // errorLog(error, DEBUG)
        result = error.message
    }

    return { result, history: mqtt.history }
}

testRun.on('run', async (testCase, testObject) => {
    return await runTest(testCase, testObject)
})

testRun.on('break', async (testCase, testObject) => {
    // Re-run the test for debugging purposes
    const result = await runTest(testCase, testObject)
    return result
})

testRun.on('cleanup', async (testObject) => {
    const { receive } = testObject
    await receive.close()
})

module.exports = () => testRun.asyncRun( 
    [
        'mqtt-client-cases'
    ], 
    __dirname, 10, 'js' )
