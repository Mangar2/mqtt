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

const types = require('@mangar2/types')
const readConfiguration = require('@mangar2/config').readConfiguration
const sanitize = require('@mangar2/configuration')
const errorLog = require('@mangar2/errorlog')
const CheckInput = require('@mangar2/checkinput')
const { writeConfig, readTreeFromStdin } = require('./initconfig')

/**
 * JSON schema to check configuration input
 * @private
 */
const JSONSchema = {
    title: 'Remotecontrol configuration',
    type: 'object',
    properties: {
        commands: {
            description: 'List of supported commands',
            oneOf: [
                {
                    type: 'array',
                    items: { $ref: '#command' }
                },
                {
                    $ref: '#command'
                }
            ]
        },
        subscribeQoS: { enum: [0, 1, 2] },
        subscribeTopic: {
            description: 'Topic to exchange state infos and remote commands',
            type: 'string'
        },
        statusIntervalInSeconds: {
            description: 'Interval between status messages in seconds',
            type: 'number'
        },
        clientId: {
            description: 'Name of the client. Client names must be unique across all mqtt clients',
            type: 'string'
        },
        version: {
            description: 'Supported interface version, 1.0 or 0.0. Version 0.0 is deprecated',
            enum: ['0.0', '1.0']
        },
        clean: {
            description: 'Clean services will not be remembered when disconnected',
            type: 'boolean'
        },
        broker: {
            description: 'Broker connection information',
            $ref: '#broker'
        },
        listener: {
            description: 'Port to listen to',
            type: ['string', 'number']
        },
        keepAliveInSeconds: {
            description: 'Amount of seconds between keep alive messages',
            type: 'number'
        },
        log: {
            description: 'Log settings',
            $ref: '#log'
        }
    },
    required: [
        'commands', 'subscribeQoS', 'subscribeTopic', 'statusIntervalInSeconds', 'clientId',
        'version', 'clean', 'broker', 'listener', 'keepAliveInSeconds'
    ],
    additionalProperties: false,
    $defs: {
        command: {
            $id: '#command',
            type: 'object',
            properties: {
                name: { type: 'string' },
                command: { type: 'string' },
                delayInSeconds: {
                    description: 'Amount of seconds to wait before executing the command',
                    type: 'number'
                },
                value: {
                    description: 'state value of the command execute return message',
                    type: 'string'
                }
            },
            required: ['name', 'command', 'value'],
            additionalProperties: false

        },
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

const checkConfiguration = new CheckInput(JSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    version: '1.0',
    clean: true,
    broker: {
        port: 8183,
        host: '127.0.0.1'
    },
    listener: 8205,
    keepAliveInSeconds: 600,
    log: [
        {
            module: 'send',
            topic: '#',
            level: 1
        },
        {
            module: 'all',
            topic: '$SYS/#',
            level: 0
        }
    ],
    subscribeQoS: 1,
    statusIntervalInSeconds: 60,
    commands: [
        {
            name: 'hibernate',
            command: 'shutdown /h',
            delayInSeconds: 60,
            value: 'hibernate'
        },
        {
            name: 'test',
            command: 'dir > remotetest.txt',
            value: 'test'
        }
    ]
}

/**
 * @private
 * @description
 * Gets the configuration, fills default values and sanitizes it
 * @param {string} filename name of the configuration file
 * @returns {Object} configuration
 */
async function getConfiguration (filename) {
    const configRead = readConfiguration(filename)
    let config = configRead !== undefined ? configRead.remoteControl : undefined
    if (config === undefined) {
        const requiredInput = {
            required: ['version', 'clientId', 'subscribeTopic'],
            broker: {
                required: ['port', 'host']
            }
        }
        config = await readTreeFromStdin(requiredInput, JSONSchema, defaultConfiguration)
        writeConfig({ default: { remoteControl: config } }, 'yahaconfig.json')
    }
    if (!types.isObject(config)) {
        errorLog('The active configuration is not an object, program stopped')
        process.exit(1)
    }
    config = sanitize(config, defaultConfiguration, checkConfiguration)
    return config
}

module.exports = getConfiguration
