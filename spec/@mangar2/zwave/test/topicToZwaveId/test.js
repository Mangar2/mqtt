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

const VERBOSE = true
const types = require('@mangar2/types')
const ZwaveDevices = require('../../zwavedevices')
const TestRun = require('@mangar2/testrun')

const testRun = new TestRun(VERBOSE)

testRun.on('prepare', (testCase) => {
    const devices = new ZwaveDevices(testCase)
    return devices
})

testRun.on('break', (test, devices) => {
    
})

testRun.on('run', (test, devices) => {
    let result
    result = devices.topicToZwaveId(test.nodes, test.topic, test.label)
    return result
})

testRun.on('validate', (test, result, path) => {
    let testOk = testRun._unitTest.assertEqual(result.node_id, test.result.node_id, path)
    testOk = testOk && testRun._unitTest.assertEqual(result.class_id, test.result.class_id, path)
    
    if (!testOk) {
        console.log(result)
        console.log(test.result)
        testRun.runAgain()
    }
})

testRun.run([
    'testcases'
], __dirname)

testRun.unitTest.showResult(4)
