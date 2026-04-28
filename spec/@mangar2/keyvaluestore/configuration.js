/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2023 Volker Böhm
 */

'use strict'

import { types } from '@mangar2/utils'
import sanitize from '@mangar2/configuration'
import errorLog from '@mangar2/errorlog'
import CheckInput from '@mangar2/checkinput'

/**
 * JSON schema to check configuration input
 * @private
 */
const KeyValueStoreOptions = {
    title: 'Key value store configuration',
    type: 'object',
    properties: {
        port: {
            description: 'port the service shall use',
            type: 'string'
        },
        directory: {
            description: 'Directory to store files to',
            type: 'string'
        },
        keepFiles: {
            description: 'Amount of files to keep to store historic information',
            type: 'integer'
        }
    },
    additionalProperties: false,
    required: [
        'port', 'directory'
    ]
}

const checkConfiguration = new CheckInput(KeyValueStoreOptions)

/**
 * Default values
 * @private
 */
const defaultConfiguration = {
    keepFiles: 2
}

/**
 * @private
 * @description
 * Sanitizes the configuraiton options. Adds default values and checks the result against a JSON schema
 * @param {object} config configuration object to sanitize
 * @returns {Object} configuration
 */
export function sanitizeConfiguration (config) {
    if (!types.isObject(config)) {
        errorLog('The active configuration is not an object, program stopped')
        process.exit(1)
    }
    config = sanitize(config, defaultConfiguration, checkConfiguration)
    return config
}

