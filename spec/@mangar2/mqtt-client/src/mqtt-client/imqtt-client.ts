/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2024 Volker Böhm
 */

import { topics_t, SubscribeResult, IMessage, LogPattern } from '@mangar2/mqtt-utils';

/**
 * Callback function type for handling received messages.
 * @param message The received message.
 * @returns The resulting message or messages.
 */
export type receipientCallback_t = (message: IMessage) => Promise<IMessage | IMessage[]>;
export type pollCallback_t = () => Promise<IMessage | IMessage[]>;

export interface IMqttClient {
    connected: boolean;
    isShuttingDown: boolean;

    on(event: string, callback: () => void): void;
    signalShutdown(isShuttingDown?: boolean): void;
    registerRecipient(serviceName: string, topics: topics_t, callback: receipientCallback_t): Promise<SubscribeResult>;
    registerSender (intervalInMilliseconds: number, callback: pollCallback_t): void;
    subscribe(serviceName: string, topics: topics_t): Promise<SubscribeResult>;
    publish(message: IMessage, serviceName:string): Promise<string[]>;
    reconnect(): Promise<void>;
    run(memoryUsageLoopNumber?: number): Promise<void>;
    close(): Promise<void>;
    registerCloseFunction(closeFunction: () => Promise<void> | void): void;
}


/**
 * Represents the options for configuring an MQTT client.
 */
export type MqttClientOptions = {
    /**
     * The client ID used to identify the MQTT client.
     */
    clientId: string;
    /**
     * The keep-alive interval in seconds for the MQTT client.
     */
    keepAliveInSeconds?: number;
    /**
     * Specifies whether the MQTT client should perform a clean session on connect.
     */
    clean?: boolean;
    /**
     * The log patterns for logging MQTT client events.
     */
    log?: LogPattern[];
};

