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
const ZwaveDevices = require('../../zwavedevices')
const TestRun = require('@mangar2/testrun')

const testRun = new TestRun(VERBOSE)

testRun.on('prepare', (testCase) => {
    const devices = new ZwaveDevices(testCase)
    return devices
})

function run(test, devices) {
    let result
    result = devices.valueToTopicAndType(test.value)
    return result
}

testRun.on('break', (test, devices) => {
    return run(test, devices)
})

testRun.on('run', (test, devices) => {
    return run(test, devices)
})

testRun.on('validate', (test, result, path) => {
    if (!testRun._unitTest.assertEqual(result.topic, test.result)) {
        console.log('result: ' + result + ' path: ' + path)
        testRun.runAgain()
    }
})

testRun.run([
    'testcases'
], __dirname)

testRun.unitTest.showResult(6)
