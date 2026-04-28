/**
 * ---------------------------------------------------------------------------------------------------
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * File:      test.js
 *
 * Author:      Volker Böhm
 * Copyright:   Volker Böhm
 * ---------------------------------------------------------------------------------------------------
 */

'use strict'

const { Logger } = require('../dist/index')
const { UnitTest } = require('@mangar2/unittest')

const logger = new Logger()
const unitTest = new UnitTest()

logger.changePattern([
    {
        module: 'send',
        topic: '/a/send',
        level: 1
    },
    {
        module: 'received',
        topic: '/a/received',
        level: 2
    },
    {
        module: 'all',
        topic: '/a/all',
        level: 3
    },
    {
        module: 'send',
        topic: '/a/all',
        level: 1
    },
    {
        module: 'any',
        topic: '/a/any',
        level: 0
    }
])

function test() {
    unitTest.assertEqual(logger.getLogLevel('send', '/a/send'), 1, 'send 1')
    unitTest.assertFalse(logger.getLogLevel('send', '/a/received'), 'send 2')

    unitTest.assertEqual(logger.getLogLevel('received', '/a/received'), 2, 'received 1')
    unitTest.assertFalse(logger.getLogLevel('received', '/a/send'), 'received 2')

    unitTest.assertEqual(logger.getLogLevel('send', '/a/all'), 3, 'all 1')
    unitTest.assertEqual(logger.getLogLevel('received', '/a/all'), 3, 'all 2')

    unitTest.assertFalse(logger.getLogLevel('send', '/a/any'), 'any')
    unitTest.assertFalse(logger.getLogLevel('received', '/a/any'), 'any')

    return unitTest.getResultFunctions(8)
}

module.exports = () => test()
