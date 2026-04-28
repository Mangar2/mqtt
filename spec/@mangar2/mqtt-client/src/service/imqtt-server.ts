/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2023 Volker Böhm
 */

import { 
  LogPattern, 
  ConnectOptions, ConnectResult, 
  SubscribeResult, SubscribeOptions, 
  UnsubscribeResult, UnsubscribeOptions,
  Message 
} from '@mangar2/mqtt-utils';

export type headers_t = Record<string, string>;

/**
 * Interface for handling MQTT-like control messages. 
 * Provides methods to manage client connections, messaging, and subscriptions.
 */
export interface IMqttServer {

    /**
     * Authenticates a user with the given credentials.
     * 
     * @param {string} user - Username for authentication.
     * @param {string} password - Password for authentication.
     * @returns {boolean} - True if authentication is successful, otherwise false.
     */  
    login(user: string, password: string): boolean;

    /**
     * Publishes a payload with specified headers.
     * 
     * @param {any} payload - The payload to be published.
     * @param {headers_t} headers - Headers associated with the payload.
     * @returns {any} - Response to the publish request.
     */    
    publish(options: { message: Message, dup: boolean }): void;
  
    /**
     * Handles a PUBREL (Publish Release) message.
     * 
     * @param {string} token - Token associated with the message.
     * @param {number} packetid - Packet identifier of the message.
     */    
    pubrel(token: string, packetid?: number): void;
  
    /**
     * Establishes a connection with the MQTT server.
     * 
     * @param {ConnectOptions} options - Options to connect to the server
     */    
    connect(options: ConnectOptions): ConnectResult;
  
    /**
     * Disconnects a client from the MQTT server.
     * 
     * @param {string} clientid - Unique identifier for the client.
     */    
    disconnect(clientid: string): void;
  
    /**
     * Subscribes a client to specified topics.
     * 
     * @param {string} clientid - Unique identifier for the client.
     * @param {any} subscription - Subscription details.
     * @returns {qos_t} - Quality of Service level for the subscription.
     */    
    subscribe(options: SubscribeOptions): SubscribeResult;
  
    /**
     * Unsubscribes a client from specified topics.
     * 
     * @param {string} clientid - Unique identifier for the client.
     * @param {topics_t} topics - Topics to unsubscribe from.
     */    
    unsubscribe(options: UnsubscribeOptions): UnsubscribeResult;
  
    /**
     * Handles a PINGREQ (Ping Request) message.
     * 
     * @param {string} token - Token identifying the client of the ping request.
     * @returns {number} - Response to the ping request.
     */    
    pingreq(token: string): number;
  
    /**
     * Sets the logging pattern for the server.
     * 
     * @param {LogPattern} pattern - Pattern for logging.
     */    
    setLog(pattern: LogPattern): void;
  }