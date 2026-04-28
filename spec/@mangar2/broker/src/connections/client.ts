/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { Types } from '@mangar2/utils';
import { IMessage, Message, qos_t, TopicMatch } from '@mangar2/mqtt-utils';
import { MessageQueues } from './messagequeues';
import { MessageQueue } from './messagequeue';
import { MessageQueueEntry } from './messagequeueentry';

export type port_t = number;

type topicsOldFormat_t = { QoS: string | number, topics: string | string[] };
export type topics_t = { [key: string]: qos_t } | topicsOldFormat_t;

/**
 * Represents a client connection.
 * Corresponds to a connection, created for each client subscribing to the broker.
 * Stores all client status and provides functions to identify the messages to send to the client.
 * @private
 */
export class Client {
    private _clientId: string;
    private _host: string | undefined;
    private _port: port_t | undefined;
    private _will: Message | undefined;
    public static _maxQueueSize: number = 100;
    public subscribePattern: TopicMatch = new TopicMatch();
    public nextMessageId: number = 0;
    public token: { send: string, receive: string } = { send: '', receive: ''};
    public qos2Queue: Record<number, {time: number, topic: string}> = {};
    public _orderedTopicsQueue: MessageQueues = new MessageQueues();
    public _qos0Queue: MessageQueue = new MessageQueue();
    public keepAlive: number = 0;
    public lastActiveTimestamp: number = 0;
    public connectTimestamp: number = 0;
    public startToTransmitTimestamp: number = 0;
    public status = 'new';
    private clean: boolean | undefined;
    private version: string = '1.0';

    constructor(clientId: string) {
        this._clientId = clientId;
    }

    /**
     * Name of the sender host.
     * @type {string}
     */
    get host(): string | undefined { return this._host; }
    set host(host: string | undefined) { this._host = host; }

    /**
     * Number of the client port.
     * @type {number}
     */
    get port(): port_t | undefined { return this._port; }
    set port(port: port_t | undefined) { this._port = port; }

    /**
     * Id of the client. It can only be set once.
     * @throws Will throw an error if client id is attempted to be changed.
     * @type {string}
     */
    get clientId(): string { return this._clientId; }
    /*
    set clientId(clientId: string | undefined) {
        if (this._clientId === undefined && clientId !== undefined) {
            this._clientId = clientId;
        }
        if (this._clientId !== clientId) {
            throw Error(`clientId may not be changed (${clientId})`);
        }
    }
    */

    /**
     * Maximal length of a queue per topic. Longer queues will be truncated.
     * @type {number}
     */
    static set maxQueueSize(size: number) { Client._maxQueueSize = size; }
    static get maxQueueSize(): number { return Client._maxQueueSize; }

    /**
     * Queues ordered by topic.
     * @type {MessageQueues}
     */
    get orderedTopicsQueue(): MessageQueues { return this._orderedTopicsQueue; }
    set orderedTopicsQueue(queues: MessageQueues) { this._orderedTopicsQueue = queues; }

    /**
     * Queue for qos=0 messages.
     * @type {MessageQueue}
     */
    get qos0Queue(): MessageQueue { return this._qos0Queue; }
    set qos0Queue(queue: MessageQueue) { this._qos0Queue = queue; }

    /**
     * Will message configuration.
     * @type {object}
     * @property {number} qos - Will message quality of service.
     * @property {boolean} retain - True, if the will message is retained.
     * @property {string} topic - Topic of the will message.
     * @property {string|number} value - Value of the will message.
     */
    get will(): Message | undefined { return this._will; }
    set will(will: Message | undefined) { this._will = will; }

    /**
     *Token the client will use to publish data, only accessed by get.
     * @type {string}
     */
    get sendToken(): string { return this.token.send; }


    /**
     * Sets the last active timestamp to now.
     */
    actualizeLastActiveTimestamp(): void {
        this.lastActiveTimestamp = new Date().getTime();
    }

    /**
     * Checks if the client has received messages recently.
     * @returns {boolean} True if the client is active.
     */
    isAlive(): boolean {
        const mqttStandardAliveFactor = 1.5;
        const now = new Date().getTime();
        return this.keepAlive !== 0 && this.lastActiveTimestamp + this.keepAlive * mqttStandardAliveFactor >= now;
    }

    /**
     * Checks if the client is currently connected.
     * @returns {boolean} True if the client is connected.
     */
    isConnected(): boolean {
        return this.status === 'connected';
    }

    /**
     * Restores the client from a JSON object.
     * @param {Record<string, unknown>} jsonObject JSON object to restore from.
     */
    static fromJSON(jsonObject: any): Client | null {

        if (!Types.isString(jsonObject._clientId)) {
            return null
        }
        let client = null
        try {
            const client = new Client(jsonObject._clientId);
            Object.assign(client, jsonObject);
            client.subscribePattern = new TopicMatch(jsonObject.subscribePattern.topicPatternList);
            client.qos0Queue = MessageQueue.fromJSON(jsonObject.qos0Queue);
            client.orderedTopicsQueue = MessageQueues.fromJSON(jsonObject.orderedTopicsQueue);
        } catch (err) {
            client = new Client(jsonObject._clientId)
        }

        return client;
    }

    /**
     * Creates a message object from payload (plain object).
     * @param {Record<string, string>} payload Object with topic, value, and reason.
     * @returns {Message} Created message object.
     */
    rebuildPayload(payload: { topic: string, value: string, reason: string }): Message {
        const message = new Message(payload.topic, payload.value, payload.reason);
        return message;
    }


    /**
     * Cleans up the session.
     */
    private cleanSession(): void {
        this.orderedTopicsQueue = new MessageQueues();
        this.qos2Queue = {};
        this.subscribePattern = new TopicMatch();
        this.status = 'clean';
    }

    /**
     * Initializes values if not already set.
     */
    private init(): void {
        if (this.clean) {
            this.cleanSession();
        }
        this.qos0Queue.deleteAllEntries();
        this.orderedTopicsQueue.clearRetryCount();
    }

    /**
     * Sets the clean value (does NOT clean the session). If the parameter clean is
     * undefined AND the property clean is undefined it will set clean to true.
     * @param {boolean|number|string} clean New clean value (accepts true, "true", 1, "1" as true value). Any other value is false.
     */
    setClean(clean: boolean | number | string): void {
        if (clean !== undefined) {
            this.clean = (clean === true || clean === 'true' || clean === 1 || clean === '1');
        } else if (this.clean === undefined) {
            this.clean = true;
        }
    }

    /**
     * Removes old entries from the qos2Queue.
     * @param {number} timeoutInMilliseconds Amount of milliseconds to keep entries in the queue.
     */
    cleanupQos2Queue(timeoutInMilliseconds: number): void {
        const now = new Date().getTime();
        for (const index in this.qos2Queue) {
            const entry = this.qos2Queue[index];
            if (entry.time + timeoutInMilliseconds < now) {
                delete this.qos2Queue[index];
            }
        }
    }


    /**
     * sets the connection to "connected", resets the unsuccessful counter to 0, starts with
     * messageId 0 and stores the current timestamp.
     * @param {string} clientId id of the client
     * @param {string} host host name
     * @param {port_t} port port name
     * @param {boolean} clean true, if the connection will be cleaned on disconnect
     * @param {string} version client interface version
     * @param {number} keepAlive keep alive time in milliseconds. The broker will disconnect
     * any client not sending messages in the keepAlive interval. Default is one hour
     * @returns {Object} connection tokens {send, receive}
     */
    connect(clientId: string, host: string, port: port_t, clean: boolean = true, version: string = '1.0', keepAlive: number = (3600 * 1000))
    : { send: string, receive: string } {
        this.startToTransmitTimestamp = 0;

        this._clientId = clientId;
        this.host = host;
        this.port = port;
        this.version = version;
        this.keepAlive = keepAlive;
        this.setClean(clean);

        this.init();
        this.status = 'connected';
        this.actualizeLastActiveTimestamp();
        this.connectTimestamp = new Date().getTime();
        this.token = { send: 'send' + this.clientId, receive: 'receive' + this.clientId };
        return this.token;
    }

    /**
     * Validates the existence of a valid connection.
     * @param {string} clientId id of the client
     * @param {string} action action name for the error message
     * @throws {Error} Connection not valid
     */
    private validateConnection(clientId: string, action: string): void {
        if (this.clientId !== clientId) {
            throw Error(`${action}: clientId does not match (${clientId})`);
        }
        if (this.status !== 'connected') {
            throw Error(`${action} without being connected`);
        }
    }

    /**
     * Disconnects a client.
     * @throws {Error} if clientId does not match
     */
    disconnect(): void {
        this.status = 'disconnected';
        this.qos0Queue.deleteAllEntries();
        this.will = undefined;
        if (this.clean) {
            this.cleanSession();
        }
    }

    /**
     * Adds a subscription to the connection in the old format
     * @param {Object} subscribe {QoS:Qos, topics:[topic1, topic2, ...]}
     * @throws {Error} if clientId does not match or subscription is not given
     */
    private subscribeOldFormat(subscribe: { QoS: string | number, topics: string | string[] }): number[] {
        const QoS = this.convertQoSToValidQoS(subscribe.QoS);
        const topics = subscribe.topics;
        const result: number[] = [];

        if (!Array.isArray(topics)) {
            this.subscribePattern.addPattern(topics, QoS);
            result.push(QoS);
        } else {
            for (const topic of topics) {
                this.subscribePattern.addPattern(topic, QoS);
                result.push(QoS);
            }
        }
        return result;
    }

    /**
     * Adds a subscription to the connection
     * @param {any} clientId id of the client
     * @param {Object} topics {topic1:QoS1, topic2:Qos2, ...}
     * @throws {Error} if clientId does not match or subscription is not given
     */
    subscribe(clientId: any, topics: topics_t): number[] {
        let result: number[] = [];
        try {
            this.validateConnection(clientId, 'subscribe');
            if ('QoS' in topics && 'topics' in topics) {
                result = this.subscribeOldFormat(topics as topicsOldFormat_t);
            } else {
                for (const topic in topics) {
                    const qos = this.convertQoSToValidQoS(topics[topic]);
                    result.push(qos);
                    this.subscribePattern.addPattern(topic, qos);
                }
            } 
        } catch (err) {
            result = [0x80];
        }
        return result;
    }

    /**
     * Removes subscriptions from topics
     * @param {any} clientId id of the client
     * @param {string|Array} topics array of topic strings with wildcards
     * @throws {Error} if clientId does not match or subscription is not given
     */
    unsubscribe(clientId: any, topics: string | string[]): void {
        this.validateConnection(clientId, 'unsubscribe');
        this.subscribePattern.deletePattern(topics);
    }

    /**
     * Deletes an entry from the qos2 queue
     * @param {number} packetid unique identifier of the packet (unique by client)
     */
    deleteFromQos2Queue(packetid: number): void {
        delete this.qos2Queue[packetid];
    }

    /**
     * Checks, if a packet id is in the qos 2 queue.
     * @param {number} packetid unique identifier of the packet (unique by client)
     */
    isInQos2Queue(packetid: number): boolean {
        return this.qos2Queue[packetid] !== undefined;
    }

    /**
     * Adds an entry to the qos=2 queue to remember send packages and to ignore duplicates
     * @param {number} packetid id of the package
     * @param {string} topic topic of the corresponding message
     */
    addToQos2Queue(packetid: number, topic: string): void {
        this.qos2Queue[packetid] = { time: new Date().getTime(), topic };
    }

    /**
     * "Publishes" messages by adding them to the publish queues. The messages must then still be sent
     * to the client afterwards
     * @param {IMessage} message to add to a publish storage
     * @param {TopicMatch} pattern optional: alternative match filter to filter messages
     */
    publishMessage(message: IMessage, pattern?: TopicMatch): void {
        const { topic } = message;
        let qos;

        if (pattern !== undefined && pattern.constructor.name === 'TopicMatch') {
            qos = pattern.getBestMatch(topic);
        } else {
            qos = this.subscribePattern.getBestMatch(topic);
        }

        const topicIsSubscribed = qos !== undefined;

        if (topicIsSubscribed) {
            const messageQueueEntry = new MessageQueueEntry({ message });
            messageQueueEntry.qos = this.calcQoS(qos, message.qos ?? 0);

            if (messageQueueEntry.qos === 0) {
                this.qos0Queue.addMessage(messageQueueEntry, Client.maxQueueSize);
            } else {
                messageQueueEntry.packetid = this.genMessageId();
                this.orderedTopicsQueue.addMessage(messageQueueEntry, Client.maxQueueSize);
            }
        }
    }

    /**
     * Acknowledges a message, either delete it or set it to pubrel
     * @param {string} topic topic to delete message from
     * @param {number} packetid id of the message
     */
    acknowledgeMessageById(topic: string, packetid: number): void {
        this.orderedTopicsQueue.acknowledgeMessageById(topic, packetid);
    }

    /**
     * @private
     * @description
     * Calculates the next message id
     * @returns {number} message id
     */
    private genMessageId(): number {
        let result = this.nextMessageId;
        if (result >= 0 || result < 0xFFFF) {
            this.nextMessageId++;
        } else {
            result = 0;
            this.nextMessageId = 1;
        }
        return result;
    }

    /**
     * @private
     * @description
     * Converts a QoS to a valid QoS. (0,1,2) by converting it to Number and setting it to
     * "0" on any invalid entry
     * @param QoS to transform
     * @return valid QoS
     */
    private convertQoSToValidQoS(QoS: string | number): qos_t {
        let numberQoS = Number(QoS);
        if (numberQoS !== 1 && numberQoS !== 2) {
            numberQoS = 0;
        }
        return numberQoS as qos_t;
    }

    /**
     * @private
     * @description
     * Calculates the resulting QoS from message an subscription
     * @param {number} messageQoS QoS from message
     * @param {number} subscribeQoS QoS from subscription
     */
    private calcQoS(messageQoS: string | number, subscribeQoS: string | number): qos_t {
        let validMessageQoS = this.convertQoSToValidQoS(messageQoS);
        let validSubscribeQoS = this.convertQoSToValidQoS(subscribeQoS);
        let result = Math.min(validMessageQoS, validSubscribeQoS) as qos_t;
        return result;
    }

    /**
     * Get all messages to send
     * @param {number} inFlightWindow maximal amount of qos1 and qos2 messages to process
     * @param {number} timeoutInMilliseconds timeout waiting for messages
     * @returns {Message[]} messages that should be sent to the client
     */
    getAllMessagesToSend(inFlightWindow: number, timeoutInMilliseconds: number): MessageQueueEntry[] {
        let result: MessageQueueEntry[] = [];
        // qos= 0 message are deleted, even if the client is not connected
        const notOrderedMessages = this.qos0Queue.deleteAllEntries();
        if (this.isConnected()) {
            result = this.orderedTopicsQueue.getMessagesToSend(inFlightWindow, timeoutInMilliseconds);
            // only add messages if connected
            result = [...notOrderedMessages, ...result];
            // Each entry needs to know all about the receiver
            for (const entry of result) {
                // Once connected, members are no longer undefined
                entry.setClientInfo(
                    this.clientId, this.host!, this.port!, this.version, this.token.receive);
            }
        }
        return result;
    }

    /**
     * @description
     * Gets the maximal retry count for a client
     * @returns {number}
     */
    getMaxRetryCount(): number {
        return this.orderedTopicsQueue.getMaxRetryCount();
    }

}

