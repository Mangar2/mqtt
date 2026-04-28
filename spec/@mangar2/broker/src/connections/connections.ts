/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Manages connections of the yaha Mqtt broker and provides functionality to receive/send
 * messages from/to the managed connections in three service levels (qos 0,1,2)
 */

'use strict'

import { Persist, Types } from '@mangar2/utils';
import { IMessage, Message, qos_t, TopicMatch, sanitize } from '@mangar2/mqtt-utils';
import CheckInput from '@mangar2/checkinput';
import { Client, topics_t } from './client';
import { MessageQueueEntry } from './messagequeueentry'

const persistConnections = new Persist();

export interface IConfig {
  fileName?: string;
  directory?: string;
  inFlightWindow?: number;
  replyTimeoutInMilliseconds?: number;
  pubrelTimeoutInMilliseconds?: number;
  maxRetryCount?: number;
  maxQueueSize?: number;
}

/**
 * @private
 * @description
 * Check valid configuration options
 */
const checkConfig = new CheckInput({
  type: 'object',
  properties: {
    fileName: { type: 'string' },
    directory: { type: 'string' },
    inFlightWindow: { type: 'number' },
    replyTimeoutInMilliseconds: { type: 'number' },
    pubrelTimeoutInMilliseconds: { type: 'number' },
    maxRetryCount: { type: 'number' },
    maxQueueSize: { type: 'number' }
  },
  required: ['fileName', 'directory', 'inFlightWindow', 'replyTimeoutInMilliseconds', 'pubrelTimeoutInMilliseconds', 'maxRetryCount', 'maxQueueSize']
} as unknown as IConfig);

/**
 * @private
 * @description
 * Default values
 */


interface IConnectResult {
    mqttcode: number;
    present: number;
    token: { send: string, receive: string };
}

interface IDisconnected {
    [clientId: string]: { reason: string, keepAlive: number };
}

/**
 * Creates a connection object
 * This class is part of an MQTT style broker. It manages connections to the broker.
 * @param {Object} configuration configuration
 * @param {string} configuration.fileName filename for persistence
 * @param {string} configuration.directory directory (file path) for persistence
 * @param {nubmber} [configuration.replyTimeoutInMilliseconds=10000] timeout to wait for an answer
 * of a http call
 * @param {number} [configuration.inFlightWindow=1] amount of qos 1 and qos 2 messages
 * send for the same topic at the same time
 * @param {number} [configuration.pubrelTimeoutInMilliseconds=24*3600*1000] timout to wait for a
 * corresponding pubrel message, default one day
 * @param {number} [configuration.maxRetryCount=10] amount of resend of messages
 * before forcing a disconnect
 * @param {number} configuration.maxQueuSize maximal amount of entries in a message queue
 */
export class Connections {
    private configuration: IConfig;
    private retainMessages: { [key: string]: Message };
    clients: { [key: string]: Client };
    private tokenMap: { [key: string]: any };

    constructor(configuration: IConfig) {

        const defaultValues: IConfig = {
            inFlightWindow: 1,
            replyTimeoutInMilliseconds: 10000,
            pubrelTimeoutInMilliseconds: 24 * 3600 * 1000,
            maxRetryCount: 10,
            maxQueueSize: 100
          };
        
        this.configuration = sanitize(configuration, defaultValues, checkConfig) as IConfig;

        Client.maxQueueSize = this.configuration.maxQueueSize!;
        this.retainMessages = {};
        this.clients = {};
        this.tokenMap = {};
    }


    /**
     * Restores all clients from last valid saved file
     */
    async restoreFromFile(): Promise<void> {
        if (!Types.isString(this.configuration.directory) || !Types.isString(this.configuration.fileName)) {
            throw(`Restore from file requires correct configuration of directory ${this.configuration.directory} and filename ${this.configuration.fileName}`)
        }
        const data = persistConnections.readData(this.configuration.directory, this.configuration.fileName) as any;
        this.setFromJSON(data)
    }

    /**
     * Sets the object from a JSON data structure
     * @param data 
     */
    setFromJSON(data: any) {
        if (data !== undefined) {
            this.retainMessages = data.retainMessages || {};
            this.tokenMap = data.tokenMap || {};

            for (const clientId in data.clients) {
                const clientData = data.clients[clientId];
                const client = Client.fromJSON(clientData);
                if (client !== null) {
                    this.clients[clientId] = client;
                }
            }
        }
    }

    /**
     * Persists all connections
     */
    async persist(): Promise<void> {
        if (!Types.isString(this.configuration.directory) || !Types.isString(this.configuration.fileName)) {
            throw(`Persist requires correct configuration of directory ${this.configuration.directory} and filename ${this.configuration.fileName}`)
        }
        await persistConnections.saveObjectToFile(this.configuration.directory, this.configuration.fileName, this);
    }

    /**
     * @private
     * @description
     * Gets a client by id
     * @throws {Error}
     * @returns {Client} client with id clientId
     */
    getClientById(clientId: any, create: boolean = false): Client {
        if (clientId === undefined) {
            throw new Error('Connect without client id (clientId)');
        }

        if (this.clients[clientId] === undefined && create) {
            this.clients[clientId] = new Client(clientId);
        }

        if (this.clients[clientId] === undefined) {
            throw new Error('Unknown clientId ' + clientId);
        }

        return this.clients[clientId];
    }

    /**
     * @private
     * @description
     * Gets a client by token
     * @returns {Client|undefined} undefined, if client was not found
     */
    getClientByToken(token: string): Client | undefined {
        let client: Client | undefined;
        if (token !== undefined) {
            const clientId = this.tokenMap[token];
            if (clientId !== undefined) {
                client = this.getClientById(clientId);
            }
        }
        return client;
    }

    /**
     * Connects a client (Stores a connection to a client)
     * @param {any} clientId id of the client
     * @param {string} host host name
     * @param {string} port port name
     * @param {bool} clean true, if the connection will be cleaned on disconnect
     * @param {string} [version='1.0'] client interface version
     * @param {number} [keepAlive=3600000] keep alive time in milliseconds. The broker will disconnect
     * any client not sending messages in the keepAlive interval. Default is one hour
     * @throws {Error}
     * @returns {IConnectResult} {mqttcode, present} present == 1, if an old session is recovered
     */
    connect(
        clientId: any,
        host: string,
        port: number,
        clean: boolean = true,
        version: string = '1.0',
        keepAlive: number = 3600 * 1000
    ): IConnectResult {
        const sessionAvailable = (this.clients[clientId] !== undefined && this.clients[clientId].status !== 'clean');
        const present = !clean && sessionAvailable ? 1 : 0;
        const client = this.getClientById(clientId, true);
        let sendToken = client.sendToken;
        if (sendToken !== undefined) {
            delete this.tokenMap[sendToken];
        }
        const token = client.connect(clientId, host, port, clean, version, keepAlive);
        sendToken = client.sendToken;
        this.tokenMap[sendToken] = clientId;
        return { mqttcode: 0, present, token };
    }

    /**
     * Disconnects a client
     * @throws {Error}
     */
    disconnect(clientId: string): void {
        const client = this.getClientById(clientId, false);
        client.disconnect();
    }

    /**
     * Clean up jobs. Call regularly (example once a minute)
     * Automatically disconnects clients not sending data until timeout
     * @returns {IDisconnected} list of clientId being automatically disconnected.
     * An object with reason:string and keepAlive:number is included for each disconnected clientId
     */
    cleanup(): IDisconnected {
        const disconnected = this._autoDisconnect();
        this._cleanupQos2Queues();
        return disconnected;
    }

    /**
     * Check each client for auto-disconnect and sends a will message on disconnect
     * @returns {IDisconnected} list of clientId being automatically disconnected.
     * An object with reason:string and keepAlive:number is included for each disconnected clientId
     * @private
     */
    private _autoDisconnect(): IDisconnected {
        const disconnected: IDisconnected = {};
        for (const clientId in this.clients) {
            const client = this.getClientById(clientId);
            const isAlive = client.isAlive();
            if (!isAlive && client.isConnected()) {
                this.publishWill(clientId, client.will);
                this.disconnect(clientId);
                disconnected[clientId] = { reason: 'inactive', keepAlive: client.keepAlive };
            }
        }
        return disconnected;
    }


    /**
     * @private
     * @description
     * clean up the qos2 queues of the clients
     */
    private _cleanupQos2Queues(): void {
        for (const clientId in this.clients) {
            const client = this.getClientById(clientId);
            client.cleanupQos2Queue(this.configuration.pubrelTimeoutInMilliseconds!);
        }
    }

    /**
     * Subscribes to topics
     * @returns {number[]} array of QoS values
     */
    subscribe(clientId: string, topics: topics_t): number[] {
        const client = this.getClientById(clientId, false);
        if (topics.topics === undefined) {
            const match = new TopicMatch(topics);
            for (const retainTopic in this.retainMessages) {
                const message = this.retainMessages[retainTopic];
                message.retain = true;
                client.publishMessage(message, match);
            }
        }
        return client.subscribe(clientId, topics);
    }

    /**
     * Unsubscribes from topics
     */
    unsubscribe(clientId: string, topics: string[]): void {
        const client = this.getClientById(clientId, false);
        client.unsubscribe(clientId, topics);
    }

    /**
     * Handles a pubrel message, deleting the entry from the qos2 queue
     */
    onPubrel(token: string, packetid: number): void {
        const client = this.getClientByToken(token);
        if (client !== undefined) {
            client.deleteFromQos2Queue(packetid);
        }
    }

    /**
     * Sets the will message of a client
     */
    setWill(clientId: string, will: IMessage): void {
        const client = this.getClientById(clientId);
        if (client !== undefined) {
            client.will = Message.createMessage(will);
        }
    }

    /**
     * @private
     * @description
     * Publishes a will message by adding it to the clients message queues
     */
    private publishWill(clientId: string, will: Message | undefined): void {
        if (will !== undefined) {
            const { qos, retain, topic, value } = will;
            if (topic !== undefined && topic !== '') {
                const message = new Message(topic, value, 'client ' + clientId + ' was auto disconnected', qos, retain);
                this.publishMessage(message);
            }
        }
    }

    /**
     * @description
     * Checks, if a token corresponds to a client. Returns the client
     * @returns {string|undefined} clientId.
     * @throws {string} error message
     */
    checkToken(token: string): string | undefined {
        const clientId = this.tokenMap[token];
        if (clientId !== undefined) {
            return clientId;
        }
        throw Error('publish message with unknown token (' + token + ') from ' + clientId);
    }

    /**
     * @description
     * Checks and remembers a qos2 message. Returns true, if the message is a duplicate
     */
    isDuplicateQos2message(clientId: string, topic: string, qos: qos_t, dup: boolean, packetid: number): boolean {
        let isDuplicate = false;
        if (qos === 2) {
            const sendClient = this.getClientById(clientId);
            isDuplicate = dup && sendClient.isInQos2Queue(packetid);
            sendClient.addToQos2Queue(packetid, topic);
        }
        return isDuplicate;
    }

    /**
     * Adds messages that needs to be published
     */
    publishMessage(message: IMessage): void {
        for (var clientId in this.clients) {
            if (message.retain) {
                if (message.value === undefined || message.value === '') {
                    delete this.retainMessages[message.topic];
                } else {
                    this.retainMessages[message.topic] = Message.createMessage(message);
                }
            }
            const client = this.getClientById(clientId);
            client.publishMessage(message);
        }
    }

    /**
     * Acknowledges a message (qos = 1 or 2 only)
     */
    acknowledgeMessage(queueEntry: MessageQueueEntry): void {
        const client = this.getClientById(queueEntry.clientId);
        client.acknowledgeMessageById(queueEntry.message.topic, Number(queueEntry.packetid));
    }

    /**
     * Gets all messages to send for all clients
     * function to be called for each message. The function has the parameter QoS and message
     * @returns {IMessageQueueEntry[]} list of messages to send
     */
    getAllMessagesToSend(): MessageQueueEntry[] {
        let allMessages: MessageQueueEntry[] = [];
        for (const clientId in this.clients) {
            const client = this.clients[clientId];
            if (client.getMaxRetryCount() >= this.configuration.maxRetryCount!) {
                client.disconnect();
            }
            const clientMessages = client.getAllMessagesToSend(
                this.configuration.inFlightWindow!,
                this.configuration.replyTimeoutInMilliseconds!
            );
            allMessages = [...allMessages, ...clientMessages];
        }
        return allMessages;
    }

}


