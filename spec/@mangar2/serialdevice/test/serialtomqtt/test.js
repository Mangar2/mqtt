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
const ArdunioDNS = require('../../serialtomqtt')
const SerialMessage = require('../../serialmessage')

const VERBOSE = false
const testrunToMqtt = new TestRun(VERBOSE)

const options = {
    interfaces: {
        i2c: {
            commandMap: {
                L: 'i2c/brightness sensor/brightness',
                M: 'i2c/motion sensor/detection state',
                R: 'i2c/temperature and humidity sensor/read error code',
                H: 'i2c/temperature and humidity sensor/humidity in percent',
                T: 'i2c/temperature and humidity sensor/temperature in celsius',
                K: 'i2c/light/light on time in seconds',
                B: 'i2c/light/light activation brightness',
                D: 'i2c/temperature and humidity sensor/pin',
                E: 'i2c/system/error level',
                I: 'i2c/light/target intensity',
                O: 'i2c/light/light start voltage',
                P: 'i2c/light/dimming delay in milliseconds',
                S: 'i2c/Arduino base settings/I2C server address',
                A: 'i2c/Arduino base settings/I2C client address',
                l: 'i2c/light/light on time',
                WardrobeTempSys: 'i2c/status/temperature',
                Version: 'i2c/status/version'
            },
            receiverMap: {
                'level0/room1/device1/': 3,
                'level1/room1/device1/': 4,
                'level1/room1/device2/': 5,
                'level1/room1/': 's',
                '$SYS/central/': 'main'
            }
        },
        fs20: {
            commandMap: {
                '12322324/2111': 'level0/room1/fs20/task/one',
                '12322324/2112': 'level2/room1/fs20/task/two',
                '12322324/2113': 'level2/room1/fs20/task/three'
            }
        },
        switch: {
            topicMap: {
                'level0/room1/switch/one': {
                    command: 'switch',
                    value: 1,
                    address: 'main'
                },
                'level1/room1/switch/two': {
                    command: 'switch',
                    value: 2,
                    address: 'main'
                },
                'level1/room1/switch/three': {
                    command: 'switch',
                    value: 4,
                    address: 'main'
                },
                'level1/room1/switch/four': {
                    command: 'switch',
                    value: 8,
                    address: 'main'
                },
                'level1/room1/switch/five': {
                    command: 'switch',
                    value: 16,
                    address: 'main'
                },
                'level1/room1/switch/six': {
                    command: 'switch',
                    value: 32,
                    address: 'main'
                },
                'level1/room1/switch/seven': {
                    command: 'switch',
                    value: 64,
                    address: 'main'
                },
                'level1/room1/switch/eight': {
                    command: 'switch',
                    value: 128,
                    address: 'main'
                }
            }
        },
        serial: {
            commandMap: {
                a: 'Arduino Status Information/internal communication state',
                b: 'Brightness Sensor/brightness in percent',
                c: 'Arduino clock/time of day in minutes',
                d: 'Arduino Status Information/debug information',
                e: 'Arduino Status Information/received error',
                h: 'Temperature and Humidity Sensor/humidity in percent',
                l: 'Light/light on time',
                m: 'Motion Sensor/detection state',
                n: 'Motion Sensor/detection state',
                o: 'window/detection state',
                p: 'Air pressure/air pressure in millibar',
                r: 'Temperature and Humidity Sensor/read error code',
                s: 'Arduino Status Information/internal temperature in celsius',
                t: 'Temperature and Humidity Sensor/temperature in celsius',
                v: 'Light/light voltage',
                w: 'Water leakage/detection state',
                y: 'Arduino Status Information/move controller state',
                z: 'Arduino Status Information/memory left in bytes'
            },
            valueMap: {
                LightOnOff: {
                    description: 'Switches light on/off by setting the light on time in seconds',
                    usedby: ['V'],
                    map: {
                        on: 3600,
                        off: 0
                    }
                }
            }
        }
    }
}

testrunToMqtt.on('prepare', testcase => {
    const arduinoDNS = new ArdunioDNS(options)
    return arduinoDNS
})

const runTestToMQTT = (test, arduinoDNS) => {
    const serialMessage = new SerialMessage(
        test.serial.interfaceName,
        test.serial.sender,
        test.serial.receiver,
        test.serial.command,
        test.serial.value,
        test.serial.action ? test.serial.action : '')
    const result = arduinoDNS.toMqttMessages(serialMessage)
    return result
}

testrunToMqtt.on('run', runTestToMQTT)

testrunToMqtt.on('break', (test, arduinoDNS) => {
    runTestToMQTT(test, arduinoDNS)
})

testrunToMqtt.on('validate', (test, messages, path) => {
    let validate = true
    for (const index in test.expected) {
        const expectedMessage = test.expected[index]
        const mqttMessage = messages[index]
        if (mqttMessage !== undefined) {
            for (const property in expectedMessage) {
                const expectedValue = expectedMessage[property]
                if (!testrunToMqtt.unitTest.assertEqual(expectedValue, mqttMessage[property], path + '/' + property)) {
                    validate = false
                }
            }
        } else {
            validate = false
        }
    }
    if (!validate) {
        console.log(messages)
        testrunToMqtt.runAgain()
    }
})

console.log('run serialtomqtt test asynchronously')
testrunToMqtt.run(
    [
        'tomqtt'
    ],
    __dirname,
    26
)
