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

// Importing required modules and classes
const { TestRun } = require('@mangar2/unittest')
const { Publish } = require('../dist/index') // Replace with actual path
const { Message } = require('@mangar2/mqtt-utils')


const VERBOSE = true
const DEBUG = false

const testRun = new TestRun(VERBOSE, DEBUG)

// Global constants


testRun.on('prepare', async (testcase) => {

    const { configuration } = testcase
    const sendInfo = { 
        history: [], 
        test: { },
        send(method, path, headers, payload) { 
            this.history.push({ method, path, headers: {...headers}, payload: {...payload} }) 
            return path === '/publish' ? this.test.onPublish : this.test.onPubrel
        } 
    }
    const publishInstance = new Publish(configuration)
    publishInstance.on('send', (...args) => { return sendInfo.send(...args) })
    return { publishInstance, sendInfo }
})

const runTest = async (test, testObject) => {
    const { method, token, message, messages, version } = test
    const { publishInstance, sendInfo } = testObject
    const runMessages = message ? [message] : messages
    let result
    const promises = []
    sendInfo.history = []
    sendInfo.test = test
    try {
        switch (method) {
        case 'publish':
            for (const message of runMessages) {
                // Create a promise for each publish operation
                const publishPromise = publishInstance.publish(token, Message.createMessage(message), version)
                promises.push(publishPromise)
            }

            // Wait for all publish operations to complete
            result = await Promise.all(promises)
            result = result.map(subArray => subArray.join(' '))

            //result = promises
            break
        case 'close':
            result = await publishInstance.close()
            break
        default:
            throw new Error(`Unsupported method: ${method}`)
        }
    }
    catch(err) {
        result = err.message
    }

    return { result, history: sendInfo.history }
}

testRun.on('run', async (test, testObject) => {
    return runTest(test, testObject)
})

testRun.on('break', async (test, testObject) => {
    // Re-run the failed test for debugging
    return runTest(test, testObject)
})

module.exports = () => testRun.asyncRun( ['publish-cases' ], __dirname, 9, 'js' )
