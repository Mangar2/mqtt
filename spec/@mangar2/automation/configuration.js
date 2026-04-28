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
    title: 'Motion configuration',
    type: 'object',
    properties: {
        motionTopics: {
            description: 'Subscription list to receive all relevant events/movements',
            type: 'array',
            items: { type: 'string' }
        },
        subscribeQoS: { enum: [0, 1, 2] },
        presenceTopic: {
            description: 'Topic holding the presence state',
            type: 'string'
        },
        rules: {
            description: 'Filename(s) to a rule file (type string: single, type array:0..n)',
            type: ['string', 'array'],
            items: { type: 'string' }
        },
        filestore: {
            descriptions: 'Settings for using a file store',
            type: 'object',
            properties: {
                use: {
                    description: 'true, if a filestore is used instead of configuration files',
                    type: 'boolean'
                },
                path: {
                    description: 'base path for the filename',
                    type: 'string'
                },
                host: {
                    description: 'name of the host providing the file store',
                    type: 'string'
                },
                port: {
                    description: 'port number of the file store',
                    type: ['string', 'integer']
                },
                additionalProperties: false
            },
            required: ['use', 'path', 'host', 'port']
        },
        intervalInSeconds: {
            description: 'Iinterval between checks for events based on rules. Additionally all rules will be checked on every receive Message',
            type: 'integer'

        },
        longitude: { type: 'number' },
        latitude: { type: 'number' },
        additionalProperties: false
    },
    required: ['motionTopics', 'longitude', 'latitude']
})

/**
 * Default values
 */
const defaultConfiguration = {
    motionTopics: ['+/+/+/motion sensor/detection state', '$SYS/presence/set'],
    presenceTopic: '$SYS/presence',
    rules: ['rules.json'],
    intervalInSeconds: 60,
    subscribeQoS: 1,
    filestore: {
        use: false,
        path: '/automation/rules',
        host: 'localhost',
        port: '8210'
    }
}

/**
 * Checks the configuration and sets default values
 */
module.exports = (configuration) => {
    return sanitize(configuration, defaultConfiguration, checkConfiguration)
}
