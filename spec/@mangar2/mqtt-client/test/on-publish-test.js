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
const { Publish, OnPublish } = require('@mangar2/mqtt-client/dist') 
const { Message } = require('@mangar2/mqtt-utils')


const VERBOSE = false
const DEBUG = false

const testRun = new TestRun(VERBOSE, DEBUG)

// Global constants


testRun.on('prepare', async (testcase) => {

    const { configuration } = testcase
    const publishInstance = new Publish(configuration)
    const onPublish = new OnPublish()
    onPublish.history = []
    onPublish.on('publish', (message, dup) => onPublish.history.push({message: {...message}, dup}))
    publishInstance.on('send', (method, path, headers, payload) => { 
        return onPublish.handleRequest(path, headers, JSON.stringify(payload)) 
    })
    return { publishInstance, onPublish }
})

const runTest = async (test, testObject) => {
    const { method, token, message, messages, version } = test
    const { publishInstance, onPublish } = testObject
    const runMessages = message ? [message] : messages
    let result
    const promises = []
    onPublish.history = []
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
            //result = promises
            break
        default:
            throw new Error(`Unsupported method: ${method}`)
        }
    }
    catch(err) {
        result = err.message
    }

    return { result, history: onPublish.history }
}

testRun.on('run', async (test, testObject) => {
    return runTest(test, testObject)
})

testRun.on('break', async (test, testObject) => {
    // Re-run the failed test for debugging
    return runTest(test, testObject)
})

module.exports = () => testRun.asyncRun( ['on-publish-cases' ], __dirname, 4, 'js' )
