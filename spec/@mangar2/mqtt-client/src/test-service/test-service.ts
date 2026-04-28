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

import { IMessage, Message, topics_t, Logger } from '@mangar2/mqtt-utils'
import { IMqttService } from '../mqtt-service/imqtt-service'

interface ITestConfig {
    topics: topics_t;
}

class TestService implements IMqttService {
    constructor(private config: ITestConfig) {
    }
    
    handleMessage(message: IMessage): IMessage | IMessage[] {
        Logger.logger.printLog();
        Logger.logger.clearLog();
        const number: number = Number(message.value);
        const result: IMessage[] = [];
        if (number > 0 && number < 10) {
            const newMessage = new Message(message.topic, number + 1, `increasing numbers ${number}`, 1);
            result.push(newMessage);
        }
        return result;
    }

    processTasks(): { messages: IMessage[], [key: string]: any } {
        const messages: IMessage[] = [
            new Message('test/test-service/null', 1, 'process tasks', 0),
            new Message('test/test-service/eins', 1, 'process tasks', 2),
            new Message('test/test-service/zwei', 1, 'process tasks', 2)
        ]
        return { messages }
    }

    getSubscriptions () : topics_t {
        return this.config.topics;
    }
}


/**
 * Prepares the MQTT service for testing.
 * If a service instance is provided, it is returned as is.
 * Otherwise, a new instance of TestService is created using the provided config.
 * @param config The configuration for the MQTT service.
 * @param service An optional existing instance of IMqttService.
 * @returns The prepared IMqttService instance.
 */
export const prepare = (config: ITestConfig, service: IMqttService | null = null): IMqttService => {
    const result = service || new TestService(config)
    return result
}
