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

const testRule = require('../testrule')
const Automation = require('@mangar2/automation')
const UnitTest = require('@mangar2/unittest')
const unitTest = new UnitTest(true, true)

require('./testRules')(unitTest)

const presence = {
    room1: {
        presenceOn: {
            rules: {
                awakeSwitch: {
                    value: ['switch', '$SYS/presence', { absent: 'off', awake: 'on', sleeping: 'off' }],
                    topic: 'room1/device1/set'
                }
            }
        }
    }
}

const dayOn = {
    room2: {
        dayOn: {
            rules: {
                onMorning: {
                    time: ['switch', '$SYS/presence', { awake: '5:00' }],
                    value: 'on',
                    topic: ['room2/device2/set', 'room2/device1/set']
                },
                offEvening: {
                    time: '23:00',
                    value: 'off',
                    topic: ['room2/device2/set', 'room2/device1/set']
                }
            }
        }
    }
}

const heating = {
    system: {
        rules: {
            heating: {
                cooldownInSeconds: 3600,
                check: ['and', ['<', 'outdoor/temperature', 18], ['<', 'indoor/temperature', 23]],
                topic: '$SYS/heating',
                value: 'on'
            }
        }
    }
}

const offEvening = {
    rules: {
        offEvening: {
            time: '22:00',
            check: {
                variable: 'first/hallway/main/temperature and humidity sensor/temperature in celsius',
                operator: 'above',
                value: 23
            },
            topic: 'ground/wardrobe/fs20/switch/floor heating/set',
            value: 'off'
        }
    }
}

const roller = {
    room1: {
        dayUp: {
            rules: {
                UpMorning: {
                    time: ['switch', '$SYS/presence', { absent: '8:00', awake: '6:00', sleeping: '10:00' }],
                    check: ['>=', 'time', ['-', 'sunrise', 10]],
                    value: 0,
                    topic: 'room1/roller/set'
                },
                UpAfternoon: {
                    time: ['switch', '$SYS/presence', { absent: '17:00', awake: '16:00', sleeping: '17:00' }],
                    check: ['<', 'time', 'sunset'],
                    value: 0,
                    topic: 'room1/roller/set'
                },
                DownEvening: {
                    time: '23:00',
                    value: 100,
                    topic: 'room1/roller/set'
                },
                DownSunset: {
                    time: '16:00',
                    check: ['>=', 'time', 'sunset'],
                    topic: 'room1/roller/set',
                    value: 100
                },
                DownCooling: {
                    time: ['switch', '$SYS/presence', { awake: '12:00', default: '9:30' }],
                    check: ['and',
                        ['>', 'outdoor/temperature', 22],
                        ['>', 'indoor/temperature',
                            ['switch', '$SYS/presence', { absent: 23, awake: 25, sleeping: 24 }]
                        ]
                    ],
                    topic: 'room1/roller/set',
                    value: 100
                }

            }
        }
    }
}

const szenarios = {
    workDay: {
        longitude: 8.203454,
        latitude: 49.9048667,
        date: new Date(),
        '6:00': 'awake',
        '8:00': 'absent',
        '18:01': 'awake',
        '22:31': 'sleeping'
    },
    shortestDay: {
        longitude: 8.208551,
        latitude: 49.903851,
        date: new Date('12/23/2020'),
        '6:00': 'awake',
        '8:00': 'absent',
        '18:01': 'awake',
        '22:31': 'sleeping'
    },
    longestDay: {
        longitude: 8.208551,
        latitude: 49.903851,
        date: new Date('06/23/2020'),
        '6:00': 'awake',
        '8:00': 'absent',
        '18:01': 'awake',
        '22:31': 'sleeping'
    },
    halfDay: {
        longitude: 8.203454,
        latitude: 49.9048667,
        date: new Date(),
        '6:00': 'awake',
        '13:00': 'absent',
        '18:01': 'awake',
        '22:31': 'sleeping'
    },
    dayOff: {
        longitude: 8.203454,
        latitude: 49.9048667,
        date: new Date(),
        '10:00': 'awake',
        '23:31': 'sleeping'
    },
    holiday: {
        longitude: 8.203454,
        latitude: 49.9048667,
        date: new Date(),
        '0:00': 'absent'
    }
}

/**
 * Test a list of message and their content
 * @param {Message[]} messages messages
 * @param {number} length amount of messages
 * @param {string[]} topics list of message topics
 * @param {number[]|string[]} values list of message values
 * @param {string[]} times list of time strings
 * @param {string[]} reasons list of reasons
 */
function testMessages (messages, length, topics, values, times, reasons) {
    const printTopic = messages[0] !== undefined ? messages[0].topic : ''
    unitTest.assertEqual(messages.length, length, printTopic + ' length: ' + messages.length)
    for (let index = 0; index < messages.length; index++) {
        const message = messages[index]
        const topic = Array.isArray(topics) ? topics[index] : topics
        const value = Array.isArray(values) ? values[index] : values
        const time = times[index]
        const reason = Array.isArray(reasons) ? reasons[index] : reasons
        unitTest.assertEqual(message.value, value, topic + ' value ' + value)
        unitTest.assertEqual(message.topic, topic, topic)
        unitTest.assertEqual((new Date(message.reason[0].timestamp)).toLocaleTimeString(), time, topic + ' time ' + time)
        unitTest.assertEqual(message.reason[0].message, reason, topic + ' reason ' + reason)
    }
}

// switch based on presence
let szenarioMessages = testRule(presence, szenarios.workDay, {})
testMessages(szenarioMessages, 5,
    'room1/device1/set',
    [0, 1, 0, 1, 0],
    ['00:00:00', '06:00:00', '08:00:00', '18:01:00', '22:31:00'],
    'Rule: /room1/presenceOn/awakeSwitch')

// switch on between awake and sleeping, switch two devices
szenarioMessages = testRule(dayOn, szenarios.halfDay, {})
testMessages(szenarioMessages, 4,
    ['room2/device2/set', 'room2/device1/set', 'room2/device2/set', 'room2/device1/set'],
    [1, 1, 0, 0],
    ['06:00:00', '06:00:00', '23:00:00', '23:00:00'],
    ['Rule: /room2/dayOn/onMorning', 'Rule: /room2/dayOn/onMorning', 'Rule: /room2/dayOn/offEvening', 'Rule: /room2/dayOn/offEvening'])

// move roller on shortest day of the year including down on temperature
szenarioMessages = testRule(roller, szenarios.shortestDay, { 'outdoor/temperature': 25, 'indoor/temperature': 24 })
testMessages(szenarioMessages, 4,
    'room1/roller/set',
    [0, 100, 100, 100],
    ['08:14:00', '09:30:00', '16:30:00', '23:00:00'],
    [
        'Rule: /room1/dayUp/UpMorning, time (08:14:00) is above or equal 08:13:52 (sunrise (08:23:52) - 10)',
        'Rule: /room1/dayUp/DownCooling, outdoor/temperature (25) is above 22 and indoor/temperature (24) is above $SYS/presence (absent) mapped to 23',
        'Rule: /room1/dayUp/DownSunset, time (16:30:00) is above or equal sunset (16:29:27)',
        'Rule: /room1/dayUp/DownEvening'
    ])
// move roller on longest day of the year including down on temperature
szenarioMessages = testRule(roller, szenarios.longestDay, { 'outdoor/temperature': 25, 'indoor/temperature': 24 })
testMessages(szenarioMessages, 5,
    'room1/roller/set',
    [0, 100, 0, 100, 100],
    ['06:00:00', '09:30:00', '17:00:00', '21:40:00', '23:00:00'],
    [
        'Rule: /room1/dayUp/UpMorning, time (06:00:00) is above or equal 05:08:56 (sunrise (05:18:56) - 10)',
        'Rule: /room1/dayUp/DownCooling, outdoor/temperature (25) is above 22 and indoor/temperature (24) is above $SYS/presence (absent) mapped to 23',
        'Rule: /room1/dayUp/UpAfternoon, time (17:00:00) is below sunset (21:39:57)',
        'Rule: /room1/dayUp/DownSunset, time (21:40:00) is above or equal sunset (21:39:57)',
        'Rule: /room1/dayUp/DownEvening'
    ])

// heating
szenarioMessages = testRule(heating, szenarios.shortestDay, { 'outdoor/temperature': 10, 'indoor/temperature': 21 })
testMessages(szenarioMessages, 25,
    '$SYS/heating',
    1,
    ['00:00:00', '01:00:00', '02:00:00', '03:00:00', '04:00:00', '05:00:00', '06:00:00', '07:00:00', '08:00:00', '09:00:00', '10:00:00', '11:00:00', '12:00:00',
        '13:00:00', '14:00:00', '15:00:00', '16:00:00', '17:00:00', '18:00:00', '19:00:00', '20:00:00', '21:00:00', '22:00:00', '23:00:00', '00:00:00'],
    'Rule: /system/heating, outdoor/temperature (10) is below 18 and indoor/temperature (21) is below 23'
)

let automation = new Automation(
    szenarios.shortestDay,
    roller
)
const messagesAndVariables = automation.processRules(szenarios.shortestDay.date)
unitTest.assertDeepEqual(messagesAndVariables.usedVariables, { '$SYS/presence': undefined },
    JSON.stringify(messagesAndVariables.usedVariables, null, 2))

const variables = Object.keys(automation.checkRules())
unitTest.assertTrue(variables.includes('outdoor/temperature'))
unitTest.assertTrue(variables.includes('indoor/temperature'))

automation = new Automation(
    szenarios.shortestDay,
    offEvening
)
automation.checkRules()
unitTest.assertEqual(automation.rules.rules.length, 0, 'rule is invalid')
// printMessages(messages)
unitTest.showResult(188)
