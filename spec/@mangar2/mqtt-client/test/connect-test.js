/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2024 Volker Böhm
 */

'use strict'

const { TestRun } = require('@mangar2/unittest')
const { Connect } = require('../dist/index') 

const VERBOSE = true
const PARALLEL = false

const testRun = new TestRun(VERBOSE, PARALLEL)

testRun.on('prepare', async (testSet) => {
    const connect = new Connect(testSet.clientId)
    return { connect }
})

const runTest = async (testCase, testObject) => {
    const { method, args } = testCase
    const { connect } = testObject
    let result
    connect.history = []
    connect.on('send', (path, method, headers, payload) => { 
        connect.history.push({path, method, headers: {...headers}, payload: {...payload}})
        return testCase.result
    })
    try {
        result = await connect[method](...args)
    } catch (error) {
        // errorLog(error, DEBUG)
        result = error.message
    }

    return { result, history: connect.history }
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
        'subscribe-cases', 
        'connect-cases' 
    ], 
    __dirname, 17, 'js' )
