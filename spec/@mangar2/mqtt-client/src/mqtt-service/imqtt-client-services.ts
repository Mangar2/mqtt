/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2024 Volker Böhm
 * @Overview Provides the interface for mqtt services (connect, subscribe, publish, etc.)
 */

import {
    IMessage, SubscribeResult, ConnectResult, topics_t
} from '@mangar2/mqtt-utils';

export type publishResult_t = string[];


/**
 * Represents the interface for MQTT client services.
 */
export interface IMqttClientServices {

    /**
     * Indicates whether the client supports the specified feature.
     * 
     * @param feature - The feature to check.
     * @returns A boolean value indicating whether the feature is supported.
     */
    supportsFeature(feature: string): boolean;

    /**
     * Connects to the MQTT broker.
     * 
     * @param clean - Indicates whether to clean the session.
     * @returns A promise that resolves to a ConnectResult object.
     */
    connect(clean: boolean): Promise<ConnectResult>;

    /**
     * Disconnects from the MQTT broker.
     * 
     * @returns A promise that resolves when the client is disconnected.
     */
    disconnect(): Promise<void>;

    /**
     * Subscribes to MQTT topics.
     * 
     * @param topics - The topics to subscribe to.
     * @param version - The version of the MQTT protocol.
     * @returns A promise that resolves to a SubscribeResult object.
     */
    subscribe(topics: topics_t, version?: string) : Promise<SubscribeResult>;

    /**
     * Sends a ping request to the MQTT broker.
     * 
     * @param token - The authentication token.
     * @returns A promise that resolves when the ping request is successful.
     */
    pingreq(token: string): Promise<void>;
    
    /**
     * Publishes a message to the MQTT broker.
     * 
     * @param token - The authentication token.
     * @param message - The message to publish.
     * @param serviceName - The name of the service responsible for publishing the message.
     * @param version - The version of the MQTT protocol.
     * @returns A promise that resolves to an array of strings.
     */    
    publish(token: string, message: IMessage, serviceName: string, version?: string) : Promise<string[]>;
    
    /**
     * Registers a callback function to be executed when a message is published.
     * 
     * @param callback - The callback function to be executed.
     */
    onPublish(callback: (message: IMessage) => void): void;

    /**
     * Starts the MQTT client service.
     */
    start(): void;

    /**
     * Closes the MQTT client service, closing all connections and listeners.
     */
    close(): void;

}