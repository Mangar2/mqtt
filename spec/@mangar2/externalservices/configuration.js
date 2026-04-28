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
const { readConfiguration } = require('@mangar2/config')
const CheckInput = require('@mangar2/checkinput')

/**
 * JSON schema to check configuration input
 * @private
 */
const ExternalServicesJSONSchema = {
    type: 'object',
    properties: {
        clientId: {
            description: 'Unique identifier of the client',
            type: 'string'
        },
        version: {
            description: 'The version of the interface, use "1.0", "0.0" is deprecated',
            enum: ['0.0', '1.0']
        },
        clean: {
            description: 'Clean services are removed from the broker memory if disconnected',
            type: 'boolean'
        },
        broker: {
            description: 'Broker connection information',
            $ref: '#broker'
        },
        listener: {
            description: 'Port to listen for Broker messages',
            type: ['string', 'number']
        },
        keepAliveInSeconds: {
            description: 'Interval between alive messages sent to the Broker',
            type: 'number'
        },
        log: {
            description: 'Initial logging settings',
            $ref: '#log'
        },
        services: {
            description: 'list of services to provide',
            type: 'array',
            items: {
                type: 'string'
            }
        },
        required: ['clientId', 'version', 'clean', 'broker', 'listener', 'keepAliveInSeconds', 'log', 'services'],
        additionalProperties: false
    },
    $defs: {
        broker: {
            $id: '#broker',
            type: 'object',
            properties: {
                port: {
                    description: 'The port the broker listens on',
                    type: ['string', 'number']
                },
                host: {
                    description: 'The host ip or host name of the broker',
                    type: 'string'
                }
            },
            required: ['port', 'host'],
            additionalProperties: false
        },
        log: {
            $id: '#log',
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    module: { enum: ['all', 'send', 'receive'] },
                    topic: { type: '#' },
                    level: { type: 'integer' }
                },
                required: ['module', 'topic'],
                additionalProperties: false
            }
        }
    }
}

const checkConfiguration = new CheckInput(ExternalServicesJSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    clientId: 'yaha/external services',
    version: '1.0',
    clean: false,
    broker: {
        port: 8183,
        host: '127.0.0.1'
    },
    listener: 8201,
    keepAliveInSeconds: 60,
    log: [
        {
            module: 'all',
            topic: '#',
            level: 1
        },
        {
            module: 'all',
            topic: '$SYS/#',
            level: 0
        }
    ],
    services: [
    ]
}

/**
 * @private
 * @description
 * Gets the configuration, fills default values and sanitizes it
 * @param {string} filename name of the configuration file
 * @returns {Object} configuration
 */
/**
 * @private
 * Gets the configuration, fills default values and sanitizes it
 * @param {string} filename name of the configuration file
 * @returns {Object} configuration
 */
function getConfiguration (filename) {
    // path to your configuration file
    const config = readConfiguration(filename)
    config.messageService = sanitize(config.externalServices, defaultConfiguration, checkConfiguration)
    return config
}

module.exports = getConfiguration
