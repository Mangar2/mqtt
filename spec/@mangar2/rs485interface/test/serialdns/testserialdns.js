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
const ArdunioDNS = require('../../serialdns')
const Message = require('@mangar2/message')
const SerialMessage = require('../../serialmessage')

const VERBOSE = false
const testrunToSerial = new TestRun(VERBOSE)
const testrunToMqtt = new TestRun(VERBOSE)

const options = {
    settings: {
        A: 'Arduino base settings/device address',
        B: 'Light/light activation brightness',
        D: 'Light/evening brightness',
        G: 'Arduino base settings/reporting delay in seconds',
        H: 'Light/light full on voltage',
        I: 'Light/day brightness',
        J: 'Light/light full on brightness sensor value',
        K: 'Light/initial light on time in seconds',
        L: 'Light/increase light on time in seconds',
        M: 'Light/max light on time in seconds',
        N: 'Light/night brightness',
        O: 'Light/light start voltage',
        P: 'Light/dimming delay in milliseconds',
        Q: 'Light/run light adjustment',
        R: 'Roller shutter/Roller shutter key',
        S: 'Arduino base settings/router address',
        T: 'Roller shutter/Time to close roller',
        V: 'Light/light on time',
        W: 'Light/light false off time adjustment',
        X: 'switch/status',
        Z: 'Arduino base settings/software version'
    },
    status: {
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
    interfaces: {
        LightOnOff: {
            description: 'Switches light on/off by setting the light on time in seconds',
            usedby: ['V'],
            map: {
                on: 3600,
                off: 0
            }
        }
    },
    topics: {
        'level0/room1/device1/switch/one': {
            command: 'X',
            value: 1,
            address: 10
        },
        'level1/room1/device2/switch/two': {
            command: 'X',
            value: 2,
            address: 12
        },
        'level1/room1/device2/switch/three': {
            command: 'X',
            value: 4,
            address: 12
        },
        'level1/room1/device2/switch/four': {
            command: 'X',
            value: 8,
            address: 12
        },
        'level1/room1/device2/switch/five': {
            command: 'X',
            value: 16,
            address: 12
        }
    },
    values: {
        V: [
            {
                value: 'on',
                serialValue: 3600
            },
            {
                value: 'off',
                serialValue: 0
            }
        ]
    },
    addresses: {
        'level0/room1/device1/': 10,
        'level1/room1/device1/': 11,
        'level1/room1/device2/': 12,
        'level1/room2/device1/': 13,
        'level2/room1/device1/': 14
    },
    myAddress: 1,
    interfaceVersion: 1
}

testrunToSerial.on('prepare', testcase => {
    const arduinoDNS = new ArdunioDNS(options)
    return arduinoDNS
})

const runTest = (test, arduinoDNS) => {
    const { topic, value } = test.message
    const message = new Message(topic, value)
    const result = arduinoDNS.toSerialMessage(message)
    return result
}

testrunToSerial.on('run', runTest)

testrunToSerial.on('break', (test, arduinoDNS) => {
    runTest(test, arduinoDNS)
})

testrunToSerial.on('validate', (test, message, path) => {
    let validate = true
    for (const property in test.expected) {
        const expectedValue = test.expected[property]
        if (!testrunToSerial.unitTest.assertEqual(expectedValue, message[property], path + '/' + property)) {
            validate = false
        }
    }
    if (!validate) {
        console.log(message)
        testrunToSerial.runAgain()
    }
})

testrunToSerial.asyncRun(
    [
        'toserial'
    ],
    __dirname,
    48
)

testrunToMqtt.on('prepare', testcase => {
    const arduinoDNS = new ArdunioDNS(options)
    return arduinoDNS
})

const runTestToMQTT = (test, arduinoDNS) => {
    const message = new SerialMessage()
    for (const property in test.serial) {
        message[property] = test.serial[property]
    }
    const result = arduinoDNS.toMqttMessages(message)
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
        for (const property in expectedMessage) {
            const expectedValue = expectedMessage[property]
            if (!testrunToMqtt.unitTest.assertEqual(expectedValue, mqttMessage[property], path + '/' + property)) {
                validate = false
            }
        }
    }
    if (!validate) {
        console.log(messages)
        testrunToMqtt.runAgain()
    }
})

testrunToMqtt.asyncRun(
    [
        'tomqtt'
    ],
    __dirname,
    28
)
