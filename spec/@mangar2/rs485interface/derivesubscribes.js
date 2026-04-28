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

/**
 * Derives a list of starting wildchard topics from the list of addresses
 * @param {Object} addresses list of address topics
 * @returns {string[]} list of wildchard topics matching all addresses in the address list
 * @private
 */
const _getAddressSubscribes = (addresses) => {
    const result = {}
    for (const address in addresses) {
        const chunks = address.split('/')
        for (const index in chunks) {
            if (chunks[index] !== '') {
                chunks[index] = '+'
            }
        }
        let topic = chunks.join('/')
        if (!topic.endsWith('/')) {
            topic += '/'
        }
        result[topic] = true
    }
    return result
}

/**
 * Derive the subscribe settings from the configuration options
 * @param {Object} options configuration options
 * @param {Object} options.settings setting commands
 * @param {Object} options.status status commands
 * @param {Object} options.topics full topic command/value settings
 * @param {Object} options.addresses location addresses
 * @param {0|1|2} options.qos quality of service for communication
 * @private
 */
const deriveSubscribes = (options) => {
    const startTopics = _getAddressSubscribes(options.addresses)
    const result = {}
    for (const startTopic in startTopics) {
        for (const index in options.settings) {
            const setting = options.settings[index]
            result[startTopic + setting + '/set'] = options.qos
        }
    }
    for (const topic in options.topics) {
        result[topic + '/+'] = options.qos
    }
    result['$SYS/rs485Interface/#'] = options.qos
    return result
}

module.exports = deriveSubscribes
