/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Base class for mqtt-related data exchange
 */

import { Callbacks } from '@mangar2/utils';
import { LogFilter, LogPattern, IMessage, Message } from '@mangar2/mqtt-utils';
import { Connections } from '../connections/connections';

export interface HttpReturn {
    headers: Record<string, string>,
    payload: any,
    statusCode: number
}

export interface IMqttConfiguration {
    log: LogPattern; 
    fileName: string;
    directory: string;
    replyTimeoutInMilliseconds?: number;
    inFlightWindow?: number;
    pubrelTimeoutInMilliseconds?: number;
    maxRetryCount?: number;
    maxQueueSize?: number; 
}

export class MqttBase {

    private _connections: Connections | null = null;
    private configuration: IMqttConfiguration;
    protected logFilter: LogFilter;
    protected callbacks: Callbacks;

    constructor (configuration: IMqttConfiguration) {
        this.configuration = configuration;
        this.logFilter = new LogFilter()
        if (configuration.log !== undefined) {
            this.logFilter.changePattern([configuration.log])
        }

        this.callbacks = new Callbacks(['login', 'authenticate', 'send', 'publish'])
    }

    public get connections(): Connections {
        if (this._connections === null) {
            this._connections = new Connections(this.configuration)
        }
        return this._connections;
    }

    /**
     * Sets a callback.
     * @param {'send'|'publish'} event callback name
     * @param {sendCallback|publishCallback} callback the callback for send and publish
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event: string, callback: (...args: any[]) => any) { this.callbacks.on(event, callback) }

    /**
     * @private
     * @description
     * Internal use only: publishes a message
     * @param {Message} message message to publish
     * @param {boolean} dup duplicate flag
     */
    protected publish(message: IMessage, dup: boolean, retain: boolean) {
        this.logFilter.condLogMessage('received', message, dup, retain);
        this.callbacks.invokeCallback('publish', message)
        this.connections.publishMessage(message)
    }

    /**
     * Publishes a log - message
     * @param {string} service name of the service bringing the message
     * @param {string} value value of the log message
     * @param {string} clientId name of the client involved
     */
    protected publishLogMessage (service: string, value: string, reason: string, clientId: string) {
        const topic = '$SYS/broker/' + service + '/' + clientId
        const IS_NOT_DUPLICATE = false
        const DO_NOT_RETAIN = false
        reason = reason + ' (' + clientId + ')'
        const message = new Message(topic, value, reason, 1)
        this.publish(message, IS_NOT_DUPLICATE, DO_NOT_RETAIN)
    }
}
