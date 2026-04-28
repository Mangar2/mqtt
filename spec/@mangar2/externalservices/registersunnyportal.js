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
 * @description Registers the sunnyportal service, if the configuration demands it
 * @param {MqttClient} mqttClient client providing mqtt services
 * @param {Object} config configuration data
 * @param {Object} config.sunnyportal configuration for the sunnyportal service
 */
function registerSunnyportal (mqttClient, config) {
    if (config.sunnyportal && config.externalServices.services.includes('sunnyportal')) {
        try {
            const Sunnyportal = require('@mangar2/sunnyportal')
            const sunnyportal = new Sunnyportal(config.sunnyportal)
            mqttClient.registerSender(config.sunnyportal.intervalInSeconds * 1000, async () => {
                return sunnyportal.getMessages()
            })
            console.log('Started service sunnyportal')
        } catch (err) {
            errorLog(err, false)
        }
    }
}

module.exports = registerSunnyportal
