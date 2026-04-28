
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

const Devices = require('@mangar2/devices')
const UnitTest = require('@mangar2/unittest')

const unitTest = new UnitTest(true, false)

const data = {
    level0: {
        room1: {
            devices: {
                device1: {
                    'Friendly name': 'Washing machine',
                    topic: 'cellar/washing machine',
                    id: 'Washingmachine',
                    interface: 'zwave'
                }
            }
        }
    },
    level1: {
        room1: {
            devices: {
                device1: {
                    'Friendly name': 'Socket at the wall',
                    topic: 'ground/socket/west',
                    interface: 'fs20'
                },
                device2: {
                    'Friendly name': 'Socket at the other wall',
                    topic: 'ground/socket/east',
                    interface: 'arduino'
                },
                pump: {
                    'Friendly name': 'garden pump',
                    topic: 'ground/pump',
                    interface: 'arduino'
                },
                lightHallway: {
                    'Friendly name': 'Light hallway',
                    topic: 'ground/hallway/light',
                    interface: 'arduino'
                },
                lightKitchen: {
                    'Friendly name': 'Light kitchen',
                    topic: 'ground/kitchen/light',
                    interface: 'arduino'
                },
                lightLivingroom: {
                    'Friendly name': 'Light livingroom',
                    topic: 'ground/livingroom/light',
                    retry: 3,
                    id: 2,
                    interface: 'zwave'
                }
            }
        }
    }
}

const devices = new Devices(data)
let result

devices.iterate('topic', (element, device) => {
    result = devices.topicToDevice(element)
    unitTest.assertEqual(result.topic, element, 'topic')
    unitTest.assertDeepEqual(device[0], result, 'topic')
})

devices.iterate('id', (element, device) => {
    result = devices.attributeToDevices('id', element)
    unitTest.assertDeepEqual(device, result, 'id')
})

unitTest.showResult(16)
