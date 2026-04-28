/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2024 Volker Böhm
 */

import { errorLog, Types } from '@mangar2/utils';
import { IMessage } from '@mangar2/mqtt-utils';
import { IServiceConfiguration, ServiceContainer } from './configuration-types'
import { IMqttService, prepareFunction_t } from './imqtt-service'
import { loadConfiguration } from './load-configuration'
import { IMqttClient } from '../mqtt-client/imqtt-client'

const DEBUG = true;

/**
 * Connects the background task functionality of a registered object with the
 * mqttClient handlers
 * @param {MqttServer} mqttClient mqtt object managing the services
 * @param {Object} service object in registration
 * @param {string} serviceName name of the service to start
 */
const registerBackgroundTasks =  (mqttClient: IMqttClient, service: IMqttService, serviceName: string) => {
    if (Types.isAnyFunction(service.on)) {
        service.on('publish', (message: IMessage) => mqttClient.publish(message, serviceName))
    }

    if (Types.isAnyFunction(service.close)) {
        mqttClient.registerCloseFunction(async () => { if (service.close) service.close() })
    }

    if (Types.isAnyFunction(service.run)) {
        service.run()
    }
}

/**
 * Registers subscriptions
 * @param {MqttClient} mqttClient client providing mqtt services
 * @param {Object} service registered service object
 * @param {string} serviceName name of the service to start
 * @param {function} prepare function to call on update message
 */
const registerSubscriptions = async (mqttClient: IMqttClient, service: IMqttService, serviceName: string, prepare: prepareFunction_t) => {
    const updateString = '$SYS/' + serviceName + '/update'
    const subscriptions = Types.isAnyFunction(service.getSubscriptions) ? service.getSubscriptions() : {}
    subscriptions[updateString] = 1

    mqttClient.registerRecipient(serviceName, subscriptions, async (message) => {
        if (message.topic === updateString) {
            const config = loadConfiguration()[serviceName];
            await prepare(config, service);
            const newSubscriptions = service.getSubscriptions ? service.getSubscriptions() : {};
            newSubscriptions[updateString] = 1;
            await mqttClient.subscribe(serviceName, newSubscriptions);
            return [];
        } else {
            return service.handleMessage(message);
        }
    })
}

/**
 * Registers a service with the MQTT client.
 * 
 * @param mqttClient The MQTT client instance.
 * @param config The service configuration.
 * @param serviceName The name of the service.
 * @param serviceImplementation The implementation module of the service.
 */
const  register = async (mqttClient: IMqttClient, config: IServiceConfiguration, serviceName: string, serviceImplementation: string) => {
    try {
        const { prepare } = require(serviceImplementation)
        // watchRules()
        const mqttService: IMqttService = await prepare(config)

        await registerSubscriptions(mqttClient, mqttService, serviceName, prepare)

        if (config.intervalInSeconds && mqttService.processTasks) {
            mqttClient.registerSender(config.intervalInSeconds * 1000, async () => {
                const result = mqttService.processTasks()
                return result.messages
            })
        }

        registerBackgroundTasks(mqttClient, mqttService, serviceName)

        console.log('started ' + serviceName + ' service')
    } catch (err) {
        errorLog(err, DEBUG)
    }
}


/**
 * Registers a service with the MQTT client.
 * 
 * @param mqttClient - The MQTT client instance.
 * @param configurations - The service configurations.
 * @param serviceName - The name of the service.
 * @param serviceImplementation - The implementation of the service.
 * @throws Error if no configuration is found for the service.
 */
export const  registerService = async (mqttClient: IMqttClient, configurations: ServiceContainer, serviceName: string, serviceImplementation: string) => {
    const config = configurations[serviceName];
    const isIncluded = configurations.runservices.services.includes(serviceName);
    if (!isIncluded) {
        return;
    }
    if (!config) {
        throw new Error(`no configuration found for service ${serviceName}`)
    }
    await register(mqttClient, config as IServiceConfiguration, serviceName, serviceImplementation);
}
