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
const sanitize = require('@mangar2/configuration')
const errorLog = require('@mangar2/errorlog')
const CheckInput = require('@mangar2/checkinput')

/**
 * JSON schema to check configuration input
 * @private
 */
const MessageStoreJSONSchema = {
    title: 'Messagestore configuration',
    type: 'object',
    properties: {
        subscriptions: {
            description: 'Subscriptions of the service, ',
            type: 'object',
            additionalProperties: {
                description: 'Property name is the topic and QoS is its value',
                enum: [0, 1, 2],
                minProperties: 1
            }
        },
        persist: {
            type: 'object',
            properties: {
                keepFiles: {
                    description: 'Amount of files to keep',
                    type: 'integer'
                },
                directory: {
                    description: 'Directory to store the persistence files',
                    type: 'string'
                },
                filename: {
                    description: 'Filename to store presistence data',
                    type: 'string'
                },
                interval: {
                    description: 'Interval in milliseconds to store the internal state to disk',
                    type: 'integer'
                }
            },
            additionalProperties: false,
            required: ['keepFiles', 'directory', 'filename', 'interval']
        },
        server: {
            type: 'object',
            properties: {
                port: {
                    description: 'Port providing the sensor information service for the user interface',
                    type: ['integer', 'string']
                },
                path: {
                    description: 'Root path for the sensor information service',
                    type: 'string'
                }
            },
            additionalProperties: false,
            required: ['port', 'path']
        },
        tree: {
            type: 'object',
            properties: {
                maxHistoryLength: {
                    description: 'Maximal length of the history before older entries will be removed',
                    type: 'integer'
                },
                historyHysterese: {
                    description: 'Amount of elements to remove every time the history is shrinked',
                    type: 'integer'
                },
                maxValuesPerHistoryEntry: {
                    description: 'Maximal amount of entries in a history entry',
                    type: 'integer'
                },
                lengthForFurtherCompression: {
                    description: 'Amount of elements in a history entry when trying additional compression',
                    type: 'integer'
                },
                upperBoundFactor: {
                    description: 'Multiplication factor to calculate the upper bound for an interval timespan',
                    type: 'number'
                },
                upperBoundAddInMilliseconds: {
                    description: 'Time in milliseconds to add to the upper bound of an interval timespan',
                    type: 'integer'
                },
                lowerBoundFactor: {
                    description: 'Multiplication factor to calculate the lower bound for an interval timespan',
                    type: 'number'
                },
                lowerBoundSubInMilliseconds: {
                    description: 'Time in milliseconds to subtract from the lower bound of an interval timespan',
                    type: 'integer'
                }
            },
            additionalProperties: false,
            required: [
                'maxHistoryLength', 'historyHysterese', 'maxValuesPerHistoryEntry', 'lengthForFurtherCompression',
                'upperBoundFactor', 'upperBoundAddInMilliseconds', 'lowerBoundFactor', 'lowerBoundSubInMilliseconds'
            ]
        }
    },
    additionalProperties: false,
    required: ['subscriptions', 'persist', 'server', 'tree']
}

const checkConfiguration = new CheckInput(MessageStoreJSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    subscriptions: {
        '#': 1,
        '$SYS/#': 1
    },
    persist: {
        keepFiles: 5,
        directory: '.',
        filename: 'messages',
        interval: 60000
    },
    server: {
        port: 8203,
        path: 'sensor'
    },
    tree: {
        maxHistoryLength: 50,
        historyHysterese: 10,
        maxValuesPerHistoryEntry: 256,
        lengthForFurtherCompression: 5,
        upperBoundFactor: 1.2,
        upperBoundAddInMilliseconds: 1000,
        lowerBoundFactor: 0.8,
        lowerBoundSubInMilliseconds: 1000
    }
}

/**
 * @private
 * @description
 * Gets the configuration, fills default values and sanitizes it
 * @param {string} filename name of the configuration file
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
