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

const HttpsClient = require('@mangar2/httpservice').HttpsClient
const Message = require('@mangar2/message')

/**
 * Creates a pushover class with the ability to send messages to the pushover app
 * @param {Object} config configuration
 * @param {string} config.host name of the pushover host
 * @param {string} config.path path to the interface
 * @param {number} [config.port=443] port number of open sense map (usually 443)
 * @param {string} config.token pushover token for authentication
 * @param {string} config.user pushover user for authentication
 * @param {string[]} config.devices array of device name
 * @example
 * const message = new Message("very bad incident", 1, "very bad incident detected")
 * const config = {
 *   pushover: {
 *       host: 'your host',
 *       path: '/1/messages.json',
 *       port: 443,
 *       token: 'your pushover token',
 *       user: 'your pushover user',
 *       devices: ['your device', 'your other device'],
 *       subscriptions: {
 *           '$SYS/incident/#': 1
 *       }
 *   }
 * }
 */
class Pushover {
    constructor (config) {
        this._config = config
    }

    /**
     * @private
     * @description
     * Formats a message reason as information string for an alert
     * @param {Reason} reason reason array to format
     */
    _formatReason (reason) {
        const firstReason = reason[0]
        let result = 'no information'
        if (firstReason !== undefined) {
            const time = new Date(firstReason.timestamp).toLocaleString()
            const message = firstReason.message
            result = time + ': ' + message
        }
        return result
    }

    /**
     * @private
     * @description
     * Gets the topic to return
     * @param {number} statusCode http status code
     * @param {string} device name of the device
     * @param {Reason} messageReason reason of the status
     * @param {Object} pushoverResult result information from opensensemap
     * @param {number} pushoverResult.status pushover status (1 success, 0 falure)
     * @returns {Message} result message
     */
    _getReturnMessage (statusCode, device, messageReason, pushoverResult) {
        let result
        if (statusCode < 300) {
            result = new Message('$SYS/pushover/success', statusCode, messageReason)
            result.addReason('pushover(' + device + ') status = ' + pushoverResult.status)
        } else {
            result = new Message('$SYS/pushover/error', statusCode, messageReason)
            result.addReason('pushover status(' + device + ') = ' + pushoverResult.status + ' errors = ' + JSON.stringify(pushoverResult.errors))
        }
        result.qos = 1
        return result
    }

    /**
     * Publishes a value to pushover (a messaging app)
     * @param {Message} message the data to publish to the messaging app
     * @param {string} message.topic the topic is used to select the sensor id from config
     * @param {number} message.value the value to publish ("alert", "warning", "message")
     * @returns {Message[]} messages containing the status. Send it back to the broker
     */
    async handleMessage (message) {
        const { host, token, devices, path, user, port = 443 } = this._config
        const priority = message.value === 'alert' ? 1 : -1
        const data = {
            token,
            user,
            message: this._formatReason(message.reason),
            priority: priority,
            title: message.topic + ' ' + message.value
        }
        const client = new HttpsClient(host, port)
        const headers = { 'content-type': 'application/json; charset=UTF-8' }
        const returnMessages = []

        for (const device of devices) {
            data.device = device
            const result = await client.send(path, 'POST', data, headers)
            const pushoverResult = JSON.parse(result.payload)
            const returnMessage = this._getReturnMessage(result.statusCode, device, message.reason, pushoverResult)
            returnMessages.push(returnMessage)
        }
        return returnMessages
    }

    /**
     * Get needed subscriptions for pushover
     */
    getSubscriptions () {
        return this._config.subscriptions
    }
}

module.exports = Pushover
