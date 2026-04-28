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
import { MessageQueueEntry } from './messagequeueentry';

/**
 * @private
 * @description
 * Creates an object holding many message queues, one per topic. Each queue is accessible by providing the topic
 */
export class MessageQueues {
    private _queues: { [topic: string]: MessageQueueEntry[] } = {};

    /**
     * Queues data structure
     * @Type {{topic: queue, topic: queue, ...}}
     */
    get queues() { return this._queues; }
    set queues(queues: { [topic: string]: MessageQueueEntry[] }) { this._queues = queues; }

    /**
     * Recreates the message queue from persistent data
     * @param data format {topic: queue, topic: queue, ...}
     */
    static fromJSON(data: { _queues: { [topic: string]: any[] } }): MessageQueues {
        const messageQueues = new MessageQueues()
        for (const topic in data._queues) {
            messageQueues.queues[topic] = [];
            const queue = data._queues[topic];
            for (const index in queue) {
                const entry = queue[index];
                messageQueues.queues[topic][index] = MessageQueueEntry.fromJSON(entry);
            }
        }
        return messageQueues
    }

    /**
     * Adds a message, cuts the queue to maxQueueSize, if too long
     * @param entry entry of the message queue
     * @param maxQueueSize maximal number of entries in queue
     */
    addMessage(entry: MessageQueueEntry, maxQueueSize: number) {
        const message = entry.message;
        if (message === undefined) {
            return;
        }
        const topic = message.topic;

        if (!Types.isArray(this.queues[topic])) {
            this.queues[topic] = [];
        }
        this.queues[topic].push(entry);
        if (this.queues[topic].length > maxQueueSize) {
            this.queues[topic].shift();
        }
    }

    /**
     * Acknowledges a message, either delete it or set it to pubrel
     * @param topic topic to delete message from
     * @param packetid id of the message
     */
    acknowledgeMessageById(topic: string, packetid: number) {
        const messageQueue = this.queues[topic];
        if (Types.isArray(messageQueue)) {
            for (const index in messageQueue) {
                const entry = messageQueue[index];
                if (entry.packetid === packetid) {
                    if (entry.qos === 2 && !entry.isStatusPubrel()) {
                        entry.setStatusToPubrel();
                    } else {
                        messageQueue.splice(parseInt(index), 1);
                    }
                    break;
                }
            }
        }
    }

    /**
     * @private
     * @description
     * get messages to send for a topic
     * @param topic topic of the messages
     * @param inFlightWindow maximal amount of messages to process
     * @param timeoutInMilliseconds timeout waiting for messages
     */
    private _getMessagesToSendByTopic(topic: string, inFlightWindow: number, timeoutInMilliseconds: number): MessageQueueEntry[] {
        let count = 0;
        const now = (new Date()).getTime();
        const result: MessageQueueEntry[] = [];
        for (const entry of this.queues[topic]) {
            if (entry.status === 'new') {
                entry.status = 'sending';
            } else if (entry.status === 'pubrel') {
                // do nothing, keep status
            } else if (entry.transmitTimestamp + timeoutInMilliseconds < now) {
                entry.status = 'duplicate';
            } else {
                break;
            }
            entry.setTransmissionTimestamp();
            result.push(entry);
            count++;
            if (count === inFlightWindow) break;
        }
        return result;
    }

    /**
     * get messages to send for all topics
     * @param inFlightWindow maximal amount of messages to send per topic
     * @param timeoutInMilliseconds timeout waiting for messages
     */
    getMessagesToSend(inFlightWindow: number, timeoutInMilliseconds: number): MessageQueueEntry[] {
        let result: MessageQueueEntry[] = [];
        for (const topic in this.queues) {
            const messages = this._getMessagesToSendByTopic(
                topic, inFlightWindow, timeoutInMilliseconds);
            result = [...result, ...messages];
        }
        return result;
    }

    /**
     * Gets the maximal retry count for a client
     * @returns the maximal retry count of the first elements of all topics
     */
    getMaxRetryCount(): number {
        let result = 0;
        for (const topic in this.queues) {
            const queue = this.queues[topic];
            // We only check the first entry, as all following entries may not have a higher
            // retry count
            if (queue.length > 0) {
                const entry = queue[0];
                if (entry.retryCount > result) {
                    result = entry.retryCount;
                }
            }
        }
        return result;
    }

    /**
     * clears all retry counts
     */
    clearRetryCount(): void {
        for (const topic in this.queues) {
            const queue = this.queues[topic];
            for (const entry of queue) {
                entry.retryCount = 0;
            }
        }
    }

}


