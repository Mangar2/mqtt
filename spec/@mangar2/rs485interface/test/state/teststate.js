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
const State = require('../../rs485state')

const VERBOSE = false
const testrun = new TestRun(VERBOSE)

const requestMap = { enableSend: 1, registrationInfo: 2, registrationRequest: 3 }
const resultMap = { stateUnchanged: 0, enableSend: 1, registrationInfo: 2, registrationRequest: 3, stateChanged: 4, unknown: null }

testrun.on('prepare', testcase => {
    const state = new State()
    return state
})

const runTest = (test, state) => {
    let result
    if (test.noMessage !== undefined) {
        for (let i = 0; i < test.noMessage; i++) {
            result = state.updateStateNoMessage()
        }
    } else {
        const request = requestMap[test.request]
        result = state.updateState(request)
    }
    const leftmostSibling = state.leftmostSibling
    const rightSibling = state.rightSibling
    const maySend = state.maySend
    return { result, leftmostSibling, rightSibling, maySend }
}

testrun.on('run', runTest)

testrun.on('break', (test, state) => {
    runTest(test, state)
})

testrun.on('validate', (test, result, path) => {
    let validate = true
    for (const property in test.expected) {
        const expected = test.expected[property]
        const expectedValue = property !== 'maySend' ? resultMap[expected] : expected
        if (!testrun.unitTest.assertEqual(expectedValue, result[property], path + '/' + property)) {
            validate = false
        }
    }
    if (!validate) {
        console.log(result)
        testrun.runAgain()
    }
})

testrun.run(
    [
        'stateUnknown',
        'stateReboot',
        'stateSingle',
        'stateUnregistered',
        'stateRegistered'
    ], __dirname)

testrun.unitTest.showResult(73)
