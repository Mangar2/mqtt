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
const { Message } = require('@mangar2/mqtt-utils')

const { HttpSendServices } = require('../dist/index') 
const { HttpReceiveServices } = require('../dist/index')


const VERBOSE = true
const PARALLEL = false

const testRun = new TestRun(VERBOSE, PARALLEL)

const receive = new HttpReceiveServices(0)
receive.listen()
const port = receive.port
const send = new HttpSendServices('test', { host: 'localhost', port } )

testRun.on('prepare', async (/*testSet*/) => {
    const history = []
    receive.onPublish.on('publish', (message, dup) => { history.push({message: {...message}, dup}) })
    return { history }
})

const runTest = async (testCase, testObject) => {
    const { history } = testObject
    // const { method, args } = testCase
    let result
    try {
        result = await send.publish('token', new Message('a/b', 'hello world'))
    } catch (error) {
        // errorLog(error, DEBUG)
        result = error.message
    }

    return { result, history }
}

testRun.on('run', async (testCase, testObject) => {
    return await runTest(testCase, testObject)
})

testRun.on('break', async (testCase, testObject) => {
    // Re-run the test for debugging purposes
    const result = await runTest(testCase, testObject)
    return result
})

testRun.on('cleanup', async () => {
    receive.close()
})

module.exports = () => testRun.asyncRun( 
    [
        'http-cases'
    ], 
    __dirname, 1, 'js' )
