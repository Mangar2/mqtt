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

const { types } = require('@mangar2/utils')
const sanitize = require('@mangar2/configuration')
const errorLog = require('@mangar2/errorlog')
const CheckInput = require('@mangar2/checkinput')

/**
 * JSON schema to check configuration input
 * @private
 */
const ArduinoOptionsJSONSchema = {
    title: 'Arduino RS485 service configuration',
    type: 'object',
    properties: {
        serialPortName: {
            description: 'name of the serial port to use for serial communication',
            type: 'string'
        },
        baudrate: {
            description: 'Baudrate to use for serial communication',
            type: 'integer'
        },
        myAddress: {
            description: 'RS485 address of this device',
            type: 'integer',
            min: 1,
            max: 127
        },
        maxVersion: {
            description: 'Maximal supported RS485 interface version',
            enum: [0, 1, 2]
        },
        tickDelay: {
            description: 'delay between two ticks for the inner clock managing the token',
            type: 'integer'
        },
        timeOfDayDelayInSeconds: {
            description: 'delay between two infos about the time of day',
            type: 'integer'
        },
        blinkDelayInSeconds: {
            description: 'time between on/off for blinking',
            type: 'integer'
        },
        temporaryOnInSeconds: {
            description: 'time a switch is set to on temporarily by default in seconds',
            type: 'integer'
        },
        qos: {
            description: 'quality of service used to send messages to the broker',
            enum: [0, 1, 2]
        },
        trace: {
            description: 'trace level',
            enum: ['errors', 'messages', 'internal']
        },
        topics: {
            type: 'object',
            additionalProperties: {
                type: 'object',
                properties: {
                    description: { type: 'string' },
                    command: {
                        description: 'command character',
                        type: 'string',
                        minLength: '1',
                        maxLength: '1'
                    },
                    value: {
                        description: 'value of the serial message',
                        type: 'integer',
                        min: 0,
                        max: 65535
                    },
                    address: {
                        description: 'address the message is sent to',
                        type: 'integer',
                        min: 1,
                        max: 127
                    }
                },
                required: ['command', 'value', 'address']
            }
        },
        interfaces: {
            description: 'List of interfaces supporting command strings instead of values. The command strings are mapped to values based on this interface defintion',
            type: 'object',
            additionalProperties: {
                type: 'object',
                properties: {
                    description: { type: 'string' },
                    usedby: {
                        description: 'List of command characters supporting this interface',
                        type: 'array',
                        items: {
                            type: 'string',
                            minLength: '1',
                            maxLength: '1'
                        }
                    },
                    map: {
                        description: 'maps strings to interger values',
                        type: 'object',
                        additionalProperties: {
                            type: 'integer'
                        }
                    }
                },
                required: ['usedby', 'map'],
                additionalProperties: false
            }

        },
        settings: {
            type: 'object',
            additionalProperties: {
                type: 'string',
                propertyNames: { maxLength: 1 }
            }
        },
        status: {
            type: 'object',
            additionalProperties: {
                type: 'string',
                propertyNames: { maxLength: 1 }
            }
        },
        addresses: {
            type: 'object',
            additionalProperties: {
                type: 'integer',
                min: 1,
                max: 127
            }
        }
    },
    additionalProperties: false,
    required: [
        'serialPortName', 'baudrate', 'myAddress', 'maxVersion', 'tickDelay',
        'blinkDelayInSeconds', 'temporaryOnInSeconds',
        'qos', 'trace', 'interfaces', 'settings', 'status', 'addresses'
    ]
}

const checkConfiguration = new CheckInput(ArduinoOptionsJSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    baudrate: 57600,
    myAddress: 1,
    maxVersion: 1,
    tickDelay: 100,
    qos: 1,
    trace: 'messages',
    timeOfDayDelayInSeconds: 60,
    temporaryOnInSeconds: 60 * 20,
    blinkDelayInSeconds: 3,
    interfaces: {
        LightOnOff: {
            description: 'Switches light on/off by setting the light on time in seconds',
            usedby: ['V'],
            map: {
                on: 3600,
                off: 0
            }
        },
        Roller: {
            description: 'Moves roller up and down',
            usedby: ['R'],
            map: {
                up: 0,
                down: 100
            }
        },
        Window: {
            description: 'Detect window state',
            usedby: ['o'],
            map: {
                closed: 0,
                open: 1
            }
        }
    },
    settings: {
        A: 'arduino base settings/device address',
        B: 'light/light activation brightness',
        C: 'time of day',
        D: 'light/evening brightness',
        G: 'arduino base settings/reporting delay in seconds',
        H: 'light/light full on voltage',
        I: 'light/day brightness',
        J: 'light/light full on brightness sensor value',
        K: 'light/initial light on time in seconds',
        L: 'light/increase light on time in seconds',
        M: 'light/max light on time in seconds',
        N: 'light/night brightness',
        O: 'light/light start voltage',
        P: 'light/dimming delay in milliseconds',
        Q: 'light/run light adjustment',
        R: 'roller shutter/roller shutter key',
        S: 'arduino base settings/router address',
        T: 'roller shutter/time to close roller',
        V: 'light/light on time',
        W: 'light/light false off time adjustment',
        X: 'switch/status',
        Z: 'arduino base settings/software version'
    },
    status: {
        a: 'arduino status information/internal communication state',
        b: 'brightness Sensor/brightness in percent',
        c: 'arduino clock/time of day in minutes',
        d: 'arduino status information/debug information',
        e: 'arduino status information/received error',
        h: 'temperature and humidity sensor/humidity in percent',
        l: 'light/light on time',
        m: 'motion sensor/detection state',
        n: 'motion sensor/detection state',
        o: 'window/detection state',
        p: 'air pressure/air pressure in millibar',
        r: 'temperature and humidity sensor/read error code',
        s: 'arduino status information/internal temperature in celsius',
        t: 'temperature and humidity sensor/temperature in celsius',
        v: 'light/light voltage',
        w: 'water leakage/detection state',
        y: 'arduino status information/move controller state',
        z: 'arduino status information/memory left in bytes'
    }
}

/**
 * @private
 * @description
 * Sanitizes the configuraiton options. Adds default values and checks the result against a JSON schema
 * @param {object} config configuration object to sanitize
 * @returns {Object} configuration
 */
function sanitizeConfiguration (config) {
    if (!types.isObject(config)) {
        errorLog('The active configuration is not an object, program stopped')
        process.exit(1)
    }
    config = sanitize(config, defaultConfiguration, checkConfiguration)
    return config
}

module.exports = sanitizeConfiguration
