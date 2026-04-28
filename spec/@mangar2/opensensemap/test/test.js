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

const Opensensemap = require('@mangar2/opensensemap')
const readConfiguration = require('@mangar2/config').readConfiguration
const UnitTest = require('@mangar2/unittest')
const unitTest = new UnitTest(false)

const configExample = {
    development: {
        opensensemap: {
            id: 'your+id',
            host: 'ingress.opensensemap.org',
            port: 443,
            sensors: [{
                name: 'temperature',
                uint: 'Â°C',
                topic: 'your/topic',
                id: 'your+id'
            }
            ]
        }
    }
}

console.log('This test needs a valid configuration, it will not run without change');
// path to your configuration file

(async () => {
    const result = await Opensensemap.publish(
        { topic: 'your/topic', value: 2.61, reason: [{ message: 'test1' }] },
        configExample.development.opensensemap
    )
    unitTest.assertEqual(result.topic, '$SYS/opensensemap/success', JSON.stringify(result.reason))
    unitTest.assertEqual(result.value, 201, JSON.stringify(result.reason))
    unitTest.showResult(2)
})()
