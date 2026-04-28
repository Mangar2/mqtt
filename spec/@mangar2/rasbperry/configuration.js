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
        switches: {
            description: 'Maps topics to switch command and value',
            type: 'object',
            properties: {
                type: 'object',
                properties: {
                    gpio: {
                        description: 'gpio number of the raspberry',
                        type: 'integer',
                        min: 2,
                        max: 26
                    },
                    invers: {
                        description: 'invers logic for the pin (if true) -> on is 0, off is 1',
                        type: 'boolean',
                        default: false
                    },
                    required: ['gpio'],
                    additionalProperties: false
                }
            }
        },
        additionalProperties: false
    },
    additionalProperties: false
}

const checkConfiguration = new CheckInput(RemoteServiceJSONSchema)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
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
