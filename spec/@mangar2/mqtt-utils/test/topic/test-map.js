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

const { TopicMap } = require('../../dist/index')
const { UnitTest } = require('@mangar2/unittest')

const configuration = {
    fs20: {
        'fs20/2111': 'ground/livingroom/zwave/switch/multimedia/set',
        'fs20/2112': 'ground/livingroom/fs20/switch/amplifier',
        'fs20/2113': 'ground/livingroom/fs20/switch/appletv'
    },
    location: {
        'location/33/#': 'first/bedroom/main/#',
        'location/34/#': 'first/dressingroom/main/#',
        'location/127/#': 'undefined/test/test/#',
        'location/main/#': 'system/#'
    }
}

const VERBOSE = false
const topicMap = new TopicMap(configuration)
const unitTest = new UnitTest(VERBOSE, false)

function test() {
    // test internal to external
    unitTest.assertEqual(topicMap.mapInternalToExternal('fs20/2111'), 'ground/livingroom/zwave/switch/multimedia/set', 'fs20/2111')
    unitTest.assertEqual(topicMap.mapInternalToExternal('fs20/2112'), 'ground/livingroom/fs20/switch/amplifier', 'fs20/2112')
    unitTest.assertEqual(topicMap.mapInternalToExternal('ground/livingroom/fs20/switch/amplifier'), 'ground/livingroom/fs20/switch/amplifier')
    unitTest.assertEqual(topicMap.mapInternalToExternal('any/not/defined'), 'any/not/defined')

    // test external to internal
    unitTest.assertEqual(topicMap.mapExternalToInternal('ground/livingroom/fs20/switch/amplifier'), 'fs20/2112')
    unitTest.assertEqual(topicMap.mapExternalToInternal('ground/livingroom/fs20/switch/appletv'), 'fs20/2113')

    // test '#' wildchards
    unitTest.assertEqual(topicMap.mapInternalToExternal('location/33/a/b'), 'first/bedroom/main/a/b')
    unitTest.assertEqual(topicMap.mapInternalToExternal('location/33/a'), 'first/bedroom/main/a')
    unitTest.assertEqual(topicMap.mapInternalToExternal('location/33'), 'location/33')
    unitTest.assertEqual(topicMap.mapInternalToExternal('location/main/'), 'system/')
    unitTest.assertEqual(topicMap.mapInternalToExternal('location/35/a/b'), 'location/35/a/b')
    unitTest.assertEqual(topicMap.mapInternalToExternal('/location/main/'), '/location/main/')
    // test '%' wildchards
    return unitTest.getResultFunctions(12)

}


module.exports = () => test()
