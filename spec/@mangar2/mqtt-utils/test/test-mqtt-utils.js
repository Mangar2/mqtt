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

const { UnitTest } = require('@mangar2/unittest')


const tests = [
    './test-message',
    './test-matchmessages',
    './topic/test-bestmatch',
    './topic/test-delete',
    './topic/test-match',
    './topic/test-performance',
    './topic/test-map',
    './test-logfilter',
    './test-mqtt-interface'
]

/**
 * Executes each test function in the 'tests' array asynchronously, logs the name of the test being run, and shows the result for each test.
 * It imports the test function dynamically using `require(test)` where `test` is the string representing the path to the module.
 * The test function is expected to return an object with a `showResult` method.
 * @async
 */
const showResult = async () => {
    for (let test of tests) {
        const testFunction = require(test)
        console.log(`running ${test}`)
        const { showResult } = await testFunction()
        showResult()
    }
}

/**
 * Executes each test function in the 'tests' array asynchronously, logs the name of the test being run, and collects the results of all tests.
 * It imports the test function dynamically using `require(test)` where `test` is the string representing the path to the module.
 * The test function is expected to return an object with a `getResult` method which is then called to get the result of the test.
 * @async
 * @returns {Promise} A Promise that resolves with a combined result of all tests. The format of the result depends on the implementation of `UnitTest.joinMultipleResults()`.
 */
const getResult = async () => {
    const results = []
    for (let test of tests) {
        const testFunction = require(test)
        console.log(`running ${test}`)
        const { getResult } = await testFunction()
        results.push(getResult())
    }
    return UnitTest.joinMultipleResults(...results)
}


module.exports = {
    showResult: async () => await showResult(),
    getResult: async() => await getResult()
}
