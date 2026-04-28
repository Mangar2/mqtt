/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

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
const TestRun = require('@mangar2/testrun')
const messageToGpio = require('../messagetogpio')
 
const testRun = new TestRun(VERBOSE)
 
testRun.on('prepare', (testCase) => {
    const options = testCase.options
    return options
})
 
testRun.on('break', (test, options) => {
    messageToGpio(test.message, options)
})
 
testRun.on('run', (test, options) => {
    const result = messageToGpio(test.message, options)
    return result
})
 
testRun.on('validate', (test, result, path) => {
    let validate = true
    for (const key in test.result) {
        const expectedValue = test.result[key]
        const derivedValue = result[key]
        validate &&= testRun.unitTest.assertEqual(expectedValue, derivedValue, key + ' ' + path)
    }

    if (!validate) {
        testRun.runAgain()
    }
    
})
 
testRun.run([
    'messagetogpio'
], __dirname)
 
testRun.unitTest.showResult(6)
 
