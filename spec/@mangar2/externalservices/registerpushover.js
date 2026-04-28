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

const { errorLog } = require('@mangar2/utils')

/**
 * @private
 * @description Registers the Pushover service, if the configuration demands it
 * @param {MqttClient} mqttClient client providing mqtt services
 * @param {Object} config configuration data
 * @param {Object} config.pushover configuration for the Pushover service
 */
function registerPushover (mqttClient, config) {
    if (config.pushover && config.externalServices.services.includes('pushover')) {
        try {
            const pushover = require('@mangar2/pushover')
            const subscriptions = config.pushover.subscriptions
            mqttClient.registerRecipient(subscriptions, (message) => {
                return pushover.publish(message, config.pushover)
            })
            console.log('Started service pushover')
        } catch (err) {
            errorLog(err, false)
        }
    }
}

module.exports = registerPushover
