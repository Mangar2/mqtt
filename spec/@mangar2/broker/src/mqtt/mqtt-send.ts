/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Helping functions to send mqtt data
 */

import { Interfaces } from '@mangar2/mqtt-utils';
import { MessageQueueEntry } from '../connections/messagequeueentry';
import { MqttBase, IMqttConfiguration, HttpReturn } from './mqtt-base';

export class MqttSend extends MqttBase {

    constructor(configuration: IMqttConfiguration) {
        super(configuration);
    }

    /**
     * @description Gets all connections or a single connection (depends on clientId)
     * @param {string} clientId - id of the client or an empty string
     * @returns {HttpReturn} - http return information
     */
    getConnections(clientId: string): HttpReturn {
        const result: HttpReturn = {
            headers: { 'Content-Type': 'application/json' },
            payload: {},
            statusCode: 200
        };
        const connections = this.connections;
        if (clientId === '') {
            result.payload = connections.clients;
        } else {
            try {
                result.payload = connections.getClientById(clientId, false);
            } catch (err: any) {
                result.payload = err.message;
            }
        }
        return result;
    }

    /**
     * @private
     * @description Sends a publish message by invoking a callback
     * @param {MessageQueueEntry} options - options for the callback
     * @returns {Promise<boolean>} - true if sending was successful
     */
    private async _sendPublish(options: MessageQueueEntry): Promise<boolean> {
        const { host, port, version, token, message, dup, packetid } = options;
        const retain = false;
        const publishData = Interfaces.publish(version, { token, message, dup, retain, packetid } );
        const connections = this.connections;
        let success = false;

        try {
            const result = await this.callbacks.invokeCallbackAsync(
                'send', host, port, 'publish', publishData.payload, publishData.headers);

            success = publishData.resultCheck(result);
            if (success) {
                connections.acknowledgeMessage(options);
            }
            this.logFilter.condLogMessage('send', message, dup, false, `${host}:${port}`);
        } catch (err: any) {
            this.logFilter.condLogMessage('send', { topic: '$SYS/broker/error', value: '', reason: [], qos: message.qos }, dup, err.message);
        }
        return success;
    }

    /**
     * @private
     * @description Sends a pubrel message by invoking a callback
     * @param {MessageQueueEntry} options - options for the callback
     * @returns {Promise<void>}
     */
    private async _sendPubrel(options: MessageQueueEntry): Promise<void> {
        const { host, port, version, token, packetid } = options;
        const publishData = Interfaces.pubrel(version, token, packetid);
        const connections = this.connections;
        let success = false;

        try {
            const result = await this.callbacks.invokeCallbackAsync(
                'send', host, port, 'pubrel', publishData.payload, publishData.headers);

            success = publishData.resultCheck(result);
            if (success) {
                connections.acknowledgeMessage(options);
            }
        } catch (err: any) {
            if (this.logFilter.getLogLevel('send', '$SYS/broker/error') !== undefined) {
                console.log('%s %s:%s pubrel [%s]', new Date().toLocaleString(), host, port, err.message);
            }
        }
    }

    /**
     * @description Cleans up connections on timeout
     * @private
     */
    private cleanup(): void {
        const disconnected = this.connections.cleanup();
        for (const clientId in disconnected) {
            const info = disconnected[clientId];
            this.publishLogMessage(
                'disconnect', 'cleanup', info.reason + ' (' + info.keepAlive + ')', clientId);
        }
    }

    /**
     * @description Processes all messages to send and for each message calls the provided callback functions registered by calls to 'on':
     * 'send' (host, port, action, payload, headers)
     * @returns {Promise<number>} - amount of messages sent
     */
    async sendMessages(): Promise<number> {
        const connections = this.connections;
        const messages = connections.getAllMessagesToSend();
        this.cleanup();
        let promises = [];
        for (const message of messages) {
            let result;
            if (message.isStatusPubrel()) {
                result = this._sendPubrel(message);
            } else {
                result = this._sendPublish(message);
            }
            promises.push(result);
            if (promises.length >= 10) {
                await Promise.all(promises);
                promises = [];
            }
        }
        await Promise.all(promises);
        return messages.length;
    }
}
