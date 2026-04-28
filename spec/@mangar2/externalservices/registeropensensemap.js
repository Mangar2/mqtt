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
 * @description Registers the opensense map service, if the configuration demands it
 * @param {MqttClient} mqttClient client providing mqtt services
 * @param {Object} config configuration data
 * @param {Object} config.opensensemap configuration for the opensensemap
 */
function registerOpensensemapService (mqttClient, config) {
    if (config.opensensemap && config.externalServices.services.includes('opensensemap')) {
        try {
            const opensensemap = require('@mangar2/opensensemap')
            const subscriptions = opensensemap.getSubscriptions(config.opensensemap)
            mqttClient.registerRecipient(subscriptions, (message) => {
                return opensensemap.publish(message, config.opensensemap)
            })
            console.log('Started service opensensemap')
        } catch (err) {
            errorLog(err, false)
        }
    }
}

module.exports = registerOpensensemapService
