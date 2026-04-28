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

const TestRun = require('@mangar2/testrun')
const ParseSerialData = require('../../parseserialdata')
const SerialMessage = require('../../serialmessage')

const VERBOSE = false
const testrun = new TestRun(VERBOSE)

testrun.on('prepare', testcase => {
    const parser = new ParseSerialData()
    return parser
})

const runTest = (test, parser) => {
    const result = []
    const encoder = new TextEncoder()

    for (const streamData of test.serialData) {
        const uint8array = encoder.encode(streamData)
        const object = parser.parse(uint8array)
        if (object !== null) {
            result.push(object)
        }
    }
    return result
}

testrun.on('run', runTest)

testrun.on('break', (test, parser) => {
    runTest(test, parser)
})

testrun.on('validate', (test, result, path) => {
    let validate = true

    for (const index in result) {
        const resultMessage = result[index]
        const expectedMessage = new SerialMessage(...Object.values(test.expected[index]))
        validate = validate && testrun.unitTest.assertDeepEqual(resultMessage, expectedMessage, path + '/' + index)
    }
    if (!validate) {
        console.log(JSON.stringify(result, null, 2))
        testrun.runAgain()
    }
})

console.log('running parseserialdata test')

testrun.run(
    [
        'parseserialdata'
    ],
    __dirname,
    10
)
