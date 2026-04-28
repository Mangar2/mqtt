/**
 * ---------------------------------------------------------------------------------------------------
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * File:        test.js
 *
 * Author:      Volker Böhm
 * Copyright:   Volker Böhm
 * ---------------------------------------------------------------------------------------------------
 */

const { Message } = require('../dist/index')
const { UnitTest } = require('@mangar2/unittest')
const VERBOSE = false
const unitTest = new UnitTest(VERBOSE)

function testGetTimestampFromReason () {
    const message1 = new Message('/hello/world', 'off', 'test made', 1, true, new Date(0))
    const now = new Date()
    const timestamp1 = message1.getDateOfNewestChange()
    unitTest.assertEqual(timestamp1.toISOString(), '1970-01-01T00:00:00.000Z', 'get timestamp 1')

    const message2 = new Message('/hello/worl', 1, [{ message: 'no timestamp' }])
    message2.addReason('a timestamp', now)
    const timestamp2 = message2.getDateOfNewestChange()
    unitTest.assertTrue(timestamp2 !== undefined, 'get timestamp 2')
    unitTest.assertEqual(timestamp2.toISOString(), now.toISOString(), 'get timestamp 3')
}

module.exports = () => {
    const message1 = new Message('/hello/world', 'on', 'test made')
    message1.addReason('added reason')
    const message2 = new Message('/hello/world', 'off', 'test made', 1, true, new Date(0))
    const message3 = new Message('/hello/world', '', 'empty string', 1, true, new Date(0))
    unitTest.assertEqual(message1.topic, '/hello/world', 'topic')
    unitTest.assertEqual(message1.value, 'on', 'value')
    unitTest.assertEqual(message1.reason[0].message, 'test made', 'reason 1')
    unitTest.assertEqual(message1.reason[1].message, 'added reason', 'reason 2')
    unitTest.assertEqual(message2.reason[0].timestamp, '1970-01-01T00:00:00.000Z', 'reason 3')
    unitTest.assertEqual(message3.value, '', 'empty value')
    testGetTimestampFromReason()
    return unitTest.getResultFunctions(9)
}
