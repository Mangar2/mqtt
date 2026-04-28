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
const SerialOptionsJSONSchema = {
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
        qos: {
            description: 'quality of service used to send messages to the broker',
            enum: [0, 1, 2]
        },
        keepAliveDelayInSeconds: {
            description: 'delay between two keep alive messages in seconds',
            type: 'integer'
        },
        trace: {
            description: 'trace level',
            enum: ['errors', 'messages', 'internal']
        },
        interfaces: {
            description: 'List of supported interfaces',
            type: 'object',
            properties: {
                i2c: {
                    description: 'I2C interface settings',
                    type: 'object',
                    properties: {
                        commandMap: {
                            description: 'Maps topics end to i2c commands',
                            type: 'object',
                            additionalProperties: {
                                type: 'string'
                            }
                        },
                        receiverMap: {
                            description: 'Maps topic begin to i2c addresses',
                            type: 'object',
                            additionalProperties: {
                                type: ['string', 'integer']
                            }
                        }
                    },
                    required: ['commandMap', 'receiverMap'],
                    additionalProperties: false
                },
                fs20: {
                    description: 'fs20 interface settings',
                    type: 'object',
                    properties: {
                        commandMap: {
                            description: 'Maps topics to fs20 addresses for sending and receiving',
                            type: 'object',
                            additionalProperties: {
                                type: 'string'
                            }
                        },
                        sendMap: {
                            description: 'Maps topics to fs20 addresses for sending',
                            type: 'object',
                            additionalProperties: {
                                type: 'string'
                            }
                        }
                    },
                    required: ['commandMap'],
                    additionalProperties: false
                },
                switch: {
                    description: 'local switch interface settings',
                    type: 'object',
                    properties: {
                        topicMap: {
                            description: 'Maps topics to switch command and value',
                            type: 'object',
                            properties: {
                                type: 'object',
                                properties: {
                                    command: {
                                        type: 'string',
                                        enum: ['switch']
                                    },
                                    value: {
                                        type: 'integer'
                                    },
                                    address: {
                                        type: 'string',
                                        enum: ['main']
                                    }
                                },
                                required: ['command', 'value', 'address'],
                                additionalProperties: false
                            }
                        }
                    },
                    required: ['topicMap'],
                    additionalProperties: false
                },
                serial: {
                    description: 'Serial interface settings',
                    type: 'object',
                    properties: {
                        commandMap: {
                            description: 'Maps topics end to serial commands',
                            type: 'object',
                            additionalProperties: {
                                type: 'string'
                            }
                        },
                        receiverMap: {
                            description: 'Maps topic begin to serial addresses',
                            type: 'object',
                            additionalProperties: {
                                type: ['string', 'integer']
                            }
                        },
                        valueMap: {
                            description: 'Maps topic end to values',
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
                        }
                    },
                    required: ['commandMap', 'valueMap'],
                    additionalProperties: false
                }
            }
        }
    },
    additionalProperties: false,
    required: [
        'serialPortName', 'baudrate', 'qos', 'trace', 'interfaces'
    ]
}

const checkConfiguration = new CheckInput(SerialOptionsJSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    baudrate: 38400,
    qos: 1,
    trace: 'internal',
    keepAliveDelayInSeconds: 30,
    interfaces: {
        i2c: {
            commandMap: {
                L: 'brightness sensor/brightness',
                M: 'motion sensor/detection state',
                R: 'temperature and humidity sensor/read error code',
                H: 'temperature and humidity sensor/humidity in percent',
                T: 'temperature and humidity sensor/temperature in celsius',
                K: 'light/light on time in seconds',
                B: 'light/light activation brightness',
                D: 'temperature and humidity sensor/pin',
                E: 'system/error level',
                I: 'light/target intensity',
                O: 'light/light start voltage',
                P: 'light/dimming delay in milliseconds',
                S: 'Arduino base settings/I2C server address',
                A: 'Arduino base settings/I2C client address',
                l: 'light/light on time',
                WardrobeTempSys: 'i2c/status/temperature',
                Version: 'i2c/status/version'
            },
            receiverMap: {
            }
        },
        fs20: {
            commandMap: {
            }
        },
        switch: {
            topicMap: {
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
            receiverMap: {
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
