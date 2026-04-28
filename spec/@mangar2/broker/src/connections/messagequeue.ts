/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { MessageQueueEntry } from './messagequeueentry';

/**
 * Creates a message queue that is initially empty
 * @private
 */
export class MessageQueue {
    private _queue: MessageQueueEntry[] = [];

    /**
     * Queue of message queue entries
     */
    get queue (): MessageQueueEntry[] { 
        return this._queue; 
    }

    set queue (queue: MessageQueueEntry[]) { 
        this._queue = queue; 
    }

    /**
     * Recreates the message queue from persistend data
     * @param data format {topic: queue, topic: queue, ...}
     */
    static fromJSON (data: { _queue: any[] }): MessageQueue {
        const messageQueue = new MessageQueue()
        for (const entry of data._queue) {
            const queueEntry = MessageQueueEntry.fromJSON(entry);
            messageQueue.addMessage(queueEntry);
        }
        return messageQueue
    }

    /**
     * Adds a message, cuts the queue to maxQueueSize, if too long
     * @param entry entry of the message queue
     * @param maxQueueSize maximal number of entries in queue
     */
    addMessage (entry: MessageQueueEntry, maxQueueSize?: number) {
        this.queue.push(entry);
        if (maxQueueSize && this.queue.length > maxQueueSize) {
            this.queue.shift();
        }
    }

    /**
     * deletes all entries and returns all deleted entries
     * @returns {MessageQueueEntry[]}
     */
    deleteAllEntries (): MessageQueueEntry[] {
        const result = this.queue;
        this.queue = [];
        return result;
    }
}

