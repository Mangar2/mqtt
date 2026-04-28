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
 * @description Registers the remoteservice service, if the configuration demands it
 * @param {MqttClient} mqttClient client providing mqtt services
 * @param {Object} config configuration data
 * @param {Object} config.remoteservice configuration for the remoteservice
 */
function registerRemoteService (mqttClient, config) {
    if (config.remoteService && config.externalServices.services.includes('remoteService')) {
        try {
            const RemoteService = require('@mangar2/remoteservice')
            const remoteService = new RemoteService(config.remoteService)
            remoteService.on('publish', (message) => mqttClient.publish(message))
            mqttClient.registerCloseFunction(async () => { remoteService.close() })
            remoteService.run()
            console.log('Started Remote-Service')
        } catch (err) {
            errorLog(err, false)
        }
    }
}

module.exports = registerRemoteService
