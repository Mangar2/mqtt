/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2024 Volker Böhm
 */

import { IMessage, topics_t } from '@mangar2/mqtt-utils';
import { IServiceConfiguration } from '@mangar2/mqtt-client/src/mqtt-service/configuration-types'

export interface IMqttService {
    on?: (event: string, handler: (message: IMessage) => void) => void;
    close?: () => Promise<void> | void;
    run?: () => void;
    getSubscriptions?: () => topics_t;
    handleMessage: (message: IMessage) => IMessage | IMessage[];
    processTasks: () => { messages: IMessage[], [key: string]: any };
}

// Define a type alias for a function with the same signature as prepare
export type prepareFunction_t = (config: IServiceConfiguration, service?: IMqttService | null) => Promise<IMqttService>;
