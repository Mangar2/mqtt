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

const sanitize = require('@mangar2/configuration')
const CheckInput = require('@mangar2/checkinput')

/**
 * JSON schema to check configuration input
 * @private
 */
const ZwaveJSONSchema = {
    type: 'object',
    properties: {
        subscribeQoS: { 
            description: 'quality of service used to subscribe messages',
            enum: [0, 1, 2] 
        },
        qos: {
            description: 'quality of service used to send messages to the broker',
            enum: [0, 1, 2]
        },
        retain: {
            description: 'Whether message shall be retained',
            type: 'boolean'
        },     
        usb: {
            type: 'object',
            properties: {
                device: {
                    description: 'device name of the USB stick for zwave',
                    type: 'string'
                },
                topic: {
                    description: 'topic to report usb stick specific information',
                    type: 'string'
                }
            },
            required: ['device', 'topic']
        },
        devices: {
            description: 'List of configured zwave devices',
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    topic: {
                        description: 'Name (path) of the device topic',
                        type: 'string'
                    },
                    node_id: {
                        description: 'Id of the zwave device',
                        type: 'number'
                    },
                    class_id: {
                        description: 'Zwave parameter number',
                        type: 'number'
                    },
                    instance_id: {
                        description: 'Id of the zwave device instance',
                        type: 'number'
                    },
                    index: {
                        description: 'Zwave sub-parameter number',
                        type: 'number'
                    },
                    type: {
                        description: 'Zwave parameter type',
                        type: 'string'
                    },
                    label: {
                        description: 'Optinal label of the command index to use',
                        type: 'string'
                    }
                },
                required: ['topic', 'node_id']
            }
        }
    },
    required: ['subscribeQoS', 'qos', 'usb', 'devices'],
    additionalProperties: false
}

const checkConfiguration = new CheckInput(ZwaveJSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    subscribeQoS: 1,
    qos: 1,
    retain: false
}

/**
 * @private
 * @description
 * Fills the configuration with default values and sanitizes it
 * @param {string} filename name of the configuration file
 * @returns {Object} configuration
 */
function sanitizeConfiguration (config) {
    config = sanitize(config, defaultConfiguration, checkConfiguration)
    return config
}

module.exports = sanitizeConfiguration
