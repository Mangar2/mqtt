/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { Types, delay, Callbacks } from '@mangar2/utils';
import { Message, IMessage, Interfaces, IResult, RequestDataV2, Logger } from '@mangar2/mqtt-utils';
import { headers_t } from '../http-services/http-receive-services';

export interface IPublishOptions {
    retry?: number;
};

/**
 * @callback PublishMessage
 * @param message the message received
 * @param qos the quality of service information
 * @param dup flag signaling duplicates
 */
export type PublishMessage = (path: string, httpMethod: string, headers: headers_t, payload: string) => void;

type SendMessageOptions = {
    sendData: RequestDataV2, 
    serviceName: string, 
    message: IMessage 
}


export class Publish {
    private topicQueues: Record<string, any> = {};
    private callbacks: Callbacks = new Callbacks(["send"]);
    private nextPacketId: number = 1;
    private retry: number;
    public terminate: boolean = false;

    /**
     * Creates an instance of Publish.
     * @param {Configuration} configuration Configuration for the publisher.
     */
    constructor(options: IPublishOptions = { retry: 60 }) {
        this.retry = Types.isNumber(options.retry) ? options.retry : 60;
    }

    /**
     * Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback
     * @param {function} callback(parameter)
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on(event: string, callback: PublishMessage): void { this.callbacks.on(event, callback); }

    /**
     * @private
     * @description
     * Sends an object via put as application/json message
     * @param {string} path path for the http PUT
     * @param {Object} payload data to be transported via. PUT
     * @returns {Object} received answer
     */
    private async send(path: string, headers: headers_t, payload: any): Promise<IResult> {
        return this.callbacks.invokeCallbackAsync('send', 'put', path, headers, payload);
    }

    /**
     * Provides the next packet ID for MQTT messages.
     * @returns {number} The next packet ID.
     */
    private providePacketId(): number {
        this.nextPacketId = this.nextPacketId % 0xFFFF || 1;
        return this.nextPacketId++;
    }

    /**
     * Attempts to send data to a specified link with retries. If an attempt fails, it retries the operation
     * until the maximum number of retries is reached, with a delay increasing quadratically between attempts.
     * 
     * @param {string} link - The URL or endpoint to which the data is sent.
     * @param {RequestDataV2} sendData - An object containing the headers and payload for the request.
     * @returns {Promise<void>} A promise that resolves if the send operation is successful within the
     *                           allotted retries, or rejects with the last error encountered if all retries fail.
     * @throws {Error | any} Throws the error encountered in the last retry attempt. The error could be of any type
     *                       that the `send` method might throw.
     */
    private async sendWithRetry(link: string, sendData: RequestDataV2): Promise<void> {
        let lastError = null
        for (let retriesLeft = this.retry; retriesLeft > 0; retriesLeft--) {
            try {
                const result = await this.send(link, sendData.headers, sendData.payload);
                sendData.resultCheck(result);
                return
            } catch (err) {
                lastError = err;
                if (retriesLeft > 1) {
                    const delayInSeconds = Math.min((this.retry - retriesLeft) ** 2, 60)
                    await delay(delayInSeconds * 1000);
                    if (sendData.headers.dup === '0') {
                        sendData.headers.dup = '1';
                    }
                } 
            }
        }
        throw lastError;
    }

    /**
     * Sends a message with appropriate retries.
     * @param {RequestDataV2} sendData Data to be sent.
     * @returns {Promise<string>} Result message
     */
    private async sendMessage({ sendData, serviceName, message }: SendMessageOptions): Promise<string> {
        const { headers, payload } = sendData;
        const topicQueue = this.topicQueues[payload.message.topic];
        topicQueue.state = 'publish';
        let resultMessage = `delivered (qos ${headers.qos})`

        try {
            await this.sendWithRetry('/publish', sendData);
            if (headers.qos === '2') {
                const sendData = Interfaces.pubrel('1.0', payload.token, headers.packetid);
                this.topicQueues[payload.message.topic].state = 'pubrel';
                await this.sendWithRetry('/pubrel', sendData);
            }
            Logger.logger.logMessage({ direction: 'out', message, serviceName });
        }
        catch (err) {
            const message = Types.isString(err) ? err : Types.isError(err) ? err.message: 'unknown error';
            resultMessage = `Error publishing message: ${message}`;
        }
        finally {
            topicQueue.state = 'ready';
        }
        return resultMessage;
    }

    /**
     * Publishes a message to an MQTT topic. For QoS 1 and QoS 2, messages are queued per topic and sent sequentially.
     * QoS 0 messages are sent without confirmation. For QoS 1 and QoS 2, this function manages the message flow,
     * ensuring that messages are sent in order and retried as necessary.
     * 
     * @param {string} token Authentication token used for the MQTT connection.
     * @param {Message} message The message object to be published. This includes the topic, payload, and optionally QoS.
     * @param {string} [version='1.0'] The MQTT protocol version to use for the message. Defaults to '1.0'.
     * @returns {Promise<string[]>} A promise that resolves with the result of the publish operation. For QoS 0, it may resolve
     *                          immediately after sending. For QoS 1 and QoS 2, it resolves after the appropriate
     *                          acknowledgment process is completed. It returns a list of success/non success messages
     */
    public async publish(token: string, message: IMessage, serviceName: string, version = '1.0'): Promise<string[]> {
        let result = [];
        Message.validate(message);
        const { qos = 1 } = message;
        if (qos === 0) {
            const sendData = Interfaces.publish(version, { token, message });
            const sendResult = await this.send('/publish', sendData.headers, sendData.payload);
            Logger.logger.logMessage({ direction: 'out', message, serviceName });
            const returnMessage = sendResult && (sendResult.statusCode === 204 || sendResult.statusCode === 200) ? "send (qos 0)" : "not send (qos 0)";
            result.push(returnMessage);
          } else {
            // For qos > 0, messages are queued per topic and sent one at a time.
            this.topicQueues[message.topic] ??= { state: 'ready', queue: [] };
            const topicQueue = this.topicQueues[message.topic];
            const sendData = Interfaces.publish(version, { token, message, dup: false, packetid: this.providePacketId() });
            topicQueue.queue.push({ sendData, serviceName, message });

            if (topicQueue.state === 'ready') {
                while (topicQueue.queue.length > 0) {
                    // state settings ensure that we only publish one message with qos > 0 at a time.
                    const firstElement = topicQueue.queue.shift();
                    const resultMessage = await this.sendMessage(firstElement);
                    result.push(resultMessage);
                }
            } else {
                result.push(`queued (qos ${qos})`);
            }
        }
        return result;
    }
}

