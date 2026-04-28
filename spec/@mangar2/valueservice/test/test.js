/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

const { prepare } = require('../index.js')

const VERBOSE = true
const DEBUG = false
const UnitTest = require('@mangar2/unittest')
const unitTest = new UnitTest(VERBOSE, DEBUG)
const Message = require('@mangar2/message')
const expectedMessage = {
    'topic': 'a',
    '_value': 2,
    '_retain': true
}
const runMessages = {
    'a': {
        '_value': 1,
        '_retain': true
    },
    'b': {
        '_value': 'hello',
        '_retain': true
    }
}

const config = {
    valuesFileName: './test/values.json'
}

const values = prepare(config)
values.on('publish', result => { 
    const expected = runMessages[result.topic]
    for (const key in expected) {
        unitTest.assertEqual(expected[key], result[key], 'run/' + result.topic + '/' + key)
    }
})
values.run()

const resultMessages = values.processMessage(new Message('a/set', 2))
unitTest.assertEqual(resultMessages.length, 1)
const resultMessage = resultMessages[0]

for (const key in expectedMessage) {
    unitTest.assertEqual(expectedMessage[key], resultMessage[key], 'process/' + key)
}

unitTest.showResult(8)
