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
const RemoteServiceJSONSchema = {
    type: 'object',
    properties: {
        server: {
            port: {
                description: 'Port of the service to listen to'
            }
        },
        services: {
            description: 'List of configured services',
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    path: {
                        description: 'Service path (without host name)',
                        type: 'string'
                    },
                    reason: {
                        description: 'Reason information to explain the source of the command message',
                        type: 'string'
                    },
                    qos: {
                        description: 'Quality of service',
                        enum: [0, 1, 2]
                    },
                    devices: {
                        description: 'List of configured device topics',
                        type: 'object',
                        additionalProperties: {
                            description: 'Device topic',
                            type: 'string'
                        }
                    }
                },
                required: ['path', 'devices']
            }
        }
    },
    required: ['server', 'services'],
    additionalProperties: false
}

const checkConfiguration = new CheckInput(RemoteServiceJSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    server: {
        port: 9123
    }
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
