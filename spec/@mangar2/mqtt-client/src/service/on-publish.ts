/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2023 Volker Böhm
 */

import { headers_t } from '../http-services/http-receive-services';
import { Message, Interfaces, Logger, LogPattern, IResult } from '@mangar2/mqtt-utils';
import { Callbacks, Types } from '@mangar2/utils';

/**
 * Represents an entry in the QoS 2 message queue.
 */
interface Qos2QueueEntry {
    time: number;
    topic: string;
}

/**
 * @callback ProcessMessage
 * @param message the message received
 * @param dup flag signaling duplicates
 */
type ProcessMessage = (message: Message, dup: boolean) => void;


/**
 * Represents a class that handles incoming publish requests and PUBREL requests.
 */
export class OnPublish {
    private _callbacks: Callbacks;
    private _qos2Queue: { [packetid: number]: Qos2QueueEntry } = {};
    private _qos2PubrelTimeoutInMilliseconds: number;


    /**
     * Creates an instance of the OnPublish service.
     * @param logger - The logger instance.
     * @param qos2PubrelTimeoutInSeconds - The timeout value in seconds for QoS 2 PUBREL packets.
     */
    constructor(qos2PubrelTimeoutInSeconds: number = 7200) {
        this._callbacks = new Callbacks(['publish']);
        this._qos2PubrelTimeoutInMilliseconds = qos2PubrelTimeoutInSeconds * 1000;
    }

    /**
     * Checks if a request is a publish request.
     * @param {string} path - The request path.
     * @returns {boolean} True if the request is a publish request.
     */
    public isPublishRequest(path: string): boolean {
        return ['/publish', '/pubrel', '/log'].includes(path);
    }

    /**
     * Handles incoming server requests.
     * @param {string} path - The request path.
     * @param {headers_t} headers - The headers of the request.
     * @param {string} payload - The payload of the request.
     * @param {ServerResponse} res - The response object.
     */
    public handleRequest(path: string, headers: headers_t, payload: string): IResult {
        switch (path) {
            case '/publish':
                return this.onPublish(headers, JSON.parse(payload));
            case '/pubrel':
                return this.onPubrel(headers);
            case '/log':
                return this.onLog(JSON.parse(payload));
            default:
                throw new Error('Illegal interface ' + path);
        }
    }

    /**
     * Registers a callback for a specific event.
     * @param {string} event - The event name.
     * @param {ProcessMessage} callback - The callback to be invoked.
     */
    on(event: string, callback: ProcessMessage): void {
        this._callbacks.on(event, callback);
    }

    /**
     * Deletes old entries from the QoS 2 message queue.
     */
    private deleteOldQos2QueueEntries(): void {
        const now = new Date().getTime();
        for (const entry in this._qos2Queue) {
            if (this._qos2Queue[entry].time + this._qos2PubrelTimeoutInMilliseconds < now) {
                delete this._qos2Queue[entry];
            } else {
                break;
            }
        }
    }

    /**
     * Handles incoming PUBREL requests.
     * @param {headers_t} headers HTTP headers of the request.
     * @param {ServerResponse} res The server response object.
     */    
    onPubrel(headers: headers_t): IResult { // Define the type for 'res'
        const result = Interfaces.onPubrel(headers);
        if (result.packetid) {
            delete this._qos2Queue[result.packetid];
            this.deleteOldQos2QueueEntries();
        }
        return result;
    }

    /**
     * Remembers a message for handling QoS 2.
     * @param {string} topic The topic of the message.
     * @param {string} packetid The packet ID.
     */    
    private rememberMessage(topic: string, packetid: number): void {
        this._qos2Queue[packetid] = { time: Date.now(), topic };
    }

    /**
     * Handles incoming publish requests. Calls the publish callback.
     * @param {headers_t} headers HTTP headers of the request.
     * @param {any} payload The payload of the request.
     * @param {ServerResponse} res The server response object.
     */    
    onPublish(headers: headers_t, payload: any): IResult { 
        const message = payload.message ? payload.message : payload;
        const messageClone = Message.createMessage(message, headers.qos, headers.retain);

        Message.validate(messageClone);
        const result = Interfaces.onPublish(headers);

        const dup = headers.dup === '1' || headers.dup === 'true';
        const packetid = result.packetid;
        Logger.logger.logMessage( { direction: 'in', message: messageClone, dup });
        const qos2PacketPublishedBefore = dup && Types.isNumber(packetid) && this._qos2Queue[packetid] !== undefined;

        if (!qos2PacketPublishedBefore) {
            if (messageClone.qos === 2 && Types.isNumber(packetid)) {
                this.rememberMessage(messageClone.topic, packetid);
            }
            this._callbacks.invokeCallback('publish', messageClone, dup );
        }

        return result;
    }

    /**
     * Handles log pattern changes.
     * @param {LogPattern[]} payload The new log patterns.
     * @param {ServerResponse} res The server response object.
     */    
    onLog(payload: LogPattern[]): IResult { // Define the type for 'res'
        Logger.logger.changePattern(payload);
        return { statusCode: 204, headers: { 'Content-Type': 'application/json' }, payload: '' }
    }

}


