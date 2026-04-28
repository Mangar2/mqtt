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

import { Message, IMessage, qos_t } from '@mangar2/mqtt-utils'

export interface IMessageQueueEntryParam {
    message: IMessage;
    packetid?: number;
    clientId?: string;
    host?: string;
    port?: number;
    version?: string; // '1.0' or '0.0'; '1.0' is default
    token?: string;
}

/**
 * @private
 * @description
 * Creates a new message queue entry storing all information to send a message including receiver
 * @param {Object} param parameter object
 * @param {IMessage} [param.message={}] payload of the entry
 * @param {number} param.packetid id of the transmit packet
 * @param {string} param.clientId id of the client
 * @param {string} param.host host name or ip address of the client
 * @param {number} param.port port number of the client
 * @param {string} [param.version='1.0'] version number of the client interface (x.y)
 * @param {string} param.token client receive token
 */
export class MessageQueueEntry {
    message: Message;
    packetid: number;
    transmitTimestamp: number;
    status: string;
    retryCount: number;
    clientId?: string;
    host?: string;
    port?: number;
    version: string;
    token: string;

    constructor (param: IMessageQueueEntryParam) {
        const { message } = param;
        if (message instanceof Message) {
            this.message = message.clone();    
        } else {
            this.message = Message.createMessage(message);
        }
        
        // Unique packetid to identify the entry (optional)
        this.packetid = param.packetid ?? 0;
        // Timestamp the message was transmitted
        this.transmitTimestamp = 0;
        // Status of the entry
        this.status = 'new';
        // Amount of retries to transmit the playload
        this.retryCount = 0;

        this.clientId = param.clientId;
        this.host = param.host;
        this.port = param.port;
        this.version = param.version ?? '1.0';
        this.token = param.token ?? '';
    }

    get qos () { return this.message.qos ?? 0; }
    set qos (qos: qos_t) { this.message.qos = qos; }
    
    get retain () { return this.message.retain; }

    /**
     *  1 signals that it is a duplicate (has been send before), accepts also
     *  true/false or '1'/'0' instead of 1/0
     * @type {number}
     */
    get dup () { return this.retryCount > 1 }
  
    /**
     * Restore the object by setting all properties (usually from a file)
     * @param {Object} properties object with all needed properties of this class.
     */
    static fromJSON(properties: { [key: string]: any }): MessageQueueEntry {
        return new MessageQueueEntry(properties as IMessageQueueEntryParam );
    }

    /**
     * Sets infos of the client
     * @param {string} clientId id of the client
     * @param {string} host host name of the client
     * @param {number} port port number of the client
     * @param {string} version client interface version
     * @param {string} token token clients may check receiving published messages
     */
    setClientInfo(clientId: string, host: string, port: number, version: string, token: string): void {
        this.clientId = clientId;
        this.host = host;
        this.port = port;
        this.version = version;
        this.token = token;
    }

    /**
     * Sets the status to pubrel
     */
    setStatusToPubrel (): void {
        this.status = 'pubrel'
        this.retryCount = 0
    }

    /**
     * increases the retry counter and stores the actual timestamp
     */
    setTransmissionTimestamp (): void {
        const now = (new Date()).getTime()
        this.transmitTimestamp = now
        this.retryCount++
    }

    /**
     * Checks, if the status is pubrel
     * @returns {boolean}
     */
    isStatusPubrel (): boolean {
        return this.status === 'pubrel'
    }

    /**
     * Link to send the message to (/publish or /pubrel)
     * @readonly
     * @type {string}
     */
    get link (): string {
        if (this.status === 'pubrel') {
            return '/pubrel'
        } else {
            return '/publish'
        }
    }
}
