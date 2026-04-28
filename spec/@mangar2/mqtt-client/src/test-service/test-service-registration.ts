/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2024 Volker Böhm
 * @overview
 * Provides registration for a test client
 */

import { Logger } from '@mangar2/mqtt-utils'

import { registerService } from '../mqtt-service/register-service'
import { MqttClient } from '../mqtt-client/mqtt-client'
import { HttpClientOptions, HttpClient } from '../http-services/http-client-services'
import { MqttClientOptions } from '../mqtt-client/imqtt-client'
import { ServiceContainer } from '../mqtt-service/configuration-types'

const httpClientOptions: HttpClientOptions = {
    clientId: 'test-service',
    version: '1.0',
    brokerOptions: {
        port: 8183,
        host: '192.168.0.4'
    },
    listener: 0
}

const mqttClientOptions: MqttClientOptions = {
    clientId: 'test-service',
    clean: true,
    keepAliveInSeconds: 60
}



const config: ServiceContainer = {
    runservices: {
        clientId: 'test-client',
        version: '1.0',
        clean: true,
        broker: {
            port: 8183,
            host: '192.168.0.4'
        },
        listener: 0,
        keepAliveInSeconds: 60,
        services: ['test-service'],
        log: []
    },
    'test-service': {
        intervalInSeconds: 10,
        topics: {
            'test/test-service/null': 0,
            'test/test-service/eins': 1,
            'test/test-service/zwei': 2
        }
    }
}

export const run = async () => {
    Logger.logger.clientName = 'test-client';
    Logger.logger.changePattern([{ module: 'all', topic: '$SYS/#', level: 1 }, { module: 'all', topic: '#', level: 1}])
    const httpClient = new HttpClient(httpClientOptions)
    const mqttClient = new MqttClient(mqttClientOptions, httpClient)
    
    await mqttClient.run()
    await registerService(mqttClient, config, 'test-service', "@mangar2/mqtt-client/dist/test-service/test-service")
}

