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

/**
 * Creates a simple queue to store messages to send
 * @private
 */
class SendQueue {
    constructor () {
        this._queue = []
    }

    /**
     * Returns true, if a new message replaces a queued message
     * @param {SerialMessage} newMessage new message to be sent
     * @param {SerialMessage} queuedMessage original message sent to a device
     * @returns {bool} true, the new message replaces the old message
     */
    _isReplacingMessage (newMessage, queuedMessage) {
        // X-Commands are excluded, because they complement each other
        return newMessage !== undefined &&
            newMessage.sender === queuedMessage.sender &&
            newMessage.receiver === queuedMessage.receiver &&
            newMessage.command === queuedMessage.command &&
            newMessage.command !== 'X'
    }

    /**
     * Adds a message to the send queue. If there is already a message with the same command in the queue,
     * the message is replaced
     * @param {SerialMessage} message message to add to the queue.
     */
    addMessage (message) {
        let isReplacing = false
        for (const index in this._queue) {
            const queuedMessage = this._queue[index]
            isReplacing = this._isReplacingMessage(message, queuedMessage)
            if (isReplacing) {
                this._queue[index] = message
                break
            }
        }
        if (!isReplacing) {
            this._queue.push(message)
        }
    }

    /**
     * Removes the latest message from the message queue
     */
    dequeue () {
        this._queue.shift()
    }

    /**
     * Checks, if there are messages to send
     * @returns {boolean}
     */
    hasMessages () {
        return this._queue.length > 0
    }

    /**
     * Gets a message from queue
     * @returns {SerialMessage}
     */
    getMessage () {
        return this._queue[0]
    }
}

module.exports = SendQueue
