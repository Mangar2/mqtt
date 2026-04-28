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

const { HTTPClient, EchoServer } = require('../dist/index')
const { TestRun } = require('@mangar2/unittest')

const VERBOSE = true
const DEBUG = true

const testRun = new TestRun(VERBOSE, DEBUG)
const echoServer = new EchoServer(0)
echoServer.run()

testRun.on('break', () => {
    testRun.unitTest.log('break')
})

testRun.on('prepare', async (testcase) => {
    const client = new HTTPClient('localhost', echoServer.port, testcase.type)
    return client
})

testRun.on('run', async (test, testObject) => {
    const client = testObject
    const callResult = await client.sendv2(test.options)
    let callPayload
    if (callResult.headers['content-type'] === 'application/json') {
        callPayload = JSON.parse(callResult.payload)
        if (test.options.type === 'json') {
            callPayload.payload = JSON.parse(callPayload.payload)
        }
    } else {
        callPayload = callResult.payload
    }
    return callPayload
})

testRun.on('cleanup', async() => {
    echoServer.stop()
}) 

module.exports = {
    showResult: async () => {
        const res = await testRun.asyncRun(['test-http-cases'], __dirname, 6, 'js')
        await res.showResult()
    },
    getResult: async() => {
        const res = await testRun.asyncRun(['test-http-cases'], __dirname, 6, 'js')
        return await res.getResult()
    }
}

