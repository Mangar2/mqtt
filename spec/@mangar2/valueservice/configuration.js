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
 */
const checkConfiguration = new CheckInput({
    title: 'Value setting configuration',
    type: 'object',
    properties: {
        valuesFileName: {
            title: 'file holding the variables and their values',
            type: 'string'
        },
        subscribeQoS: { enum: [0, 1, 2] },
        additionalProperties: false
    },
    required: ['valuesFileName', 'subscribeQoS']
})

/**
 * Default values
 */
const defaultConfiguration = {
    valuesFileName: 'values.json',
    subscribeQoS: 1
}

/**
 * Checks the configuration and sets default values
 */
module.exports = (configuration) => {
    return sanitize(configuration, defaultConfiguration, checkConfiguration)
}
