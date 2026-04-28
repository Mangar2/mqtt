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

const DEBUG = false
const { Callbacks, errorLog, delay } = require('@mangar2/utils')
const RS485TokenExchange = require('./rs485tokenexchange')
const SendQueue = require('./sendqueue')
const { TOKEN_COMMAND } = require('./constants')

/**
 * Callback to send an array of bytes to a receiver
 * @callback Send
 * @param {Array} byteArray Array of bytes
 */

/**
 * Creates a new token handler for RS485 based serial connections to arduinos
 * @param {Object} options configuration options
 * @param {integer} options.myAddress address of this device
 * @param {integer} options.maxVersion maximal suported interface version
 * @param {integer} options.tickDelay inner clock managing the token, every action is calculated
 * in "ticks". Faster ticks leads to faster communiation but more overhead-traffic. The standard
 * value is 100 milliseconds
 * @private
 */
class RS485Schedule {
    constructor (options) {
        this._tokenExchange = new RS485TokenExchange(options)
        this._tickDelay = options.tickDelay
        this._callbacks = new Callbacks(['send', 'trace'])
        this._sendQueue = new SendQueue()
        this._send = 0
        this._sendRetryCount = 0
        this._received = 0
        this._closed = true
        this._tickCount = 0
    }

    /**
     * Sends a message to a serial port
     * @param {SerialMessage} message message to send
     * @private
     */
    async _sendMessage (message) {
        // ensure that messages are always send with the version of the oldes participant
        message.version = this._tokenExchange.version
        const byteArray = message.getByteArray()
        await this._callbacks.invokeCallbackAsync('send', byteArray)
        this._callbacks.invokeCallback('trace', { message })
        this._tokenExchange.enableChangeVersion(message)
        this.send++
    }

    /**
     * Sends a message from queue and removes it, if no reply expected
     * @private
     */
    async _sendMessageFromQueue () {
        try {
            if (this._sendQueue.hasMessages()) {
                const message = this._sendQueue.getMessage(0)
                await this._sendMessage(message)
                this._sendRetryCount++
                if (this._sendRetryCount >= 10 || !message.reply) {
                    this._sendQueue.dequeue()
                    this._sendRetryCount = 0
                }
            }
        } catch (err) {
            errorLog(err, DEBUG)
        }
    }

    /**
     * Sends a serial message by placing it on a queue
     * @param {SerialMessage} message
     */
    sendMessage (message) {
        this._sendQueue.addMessage(message)
    }

    /**
     * Processes a received message and manages the RS485 token handling
     * according to this message
     * Additionally removes messages from the send queue requesting an
     * answer
     * @param {SerialMessage} messageReceived
     * @returns {boolean} true, if the message should be send to the broker
     */
    processReceivedMessage (messageReceived) {
        const messageToSend = this._tokenExchange.processStateMessage(messageReceived)
        if (messageToSend) {
            this._sendMessage(messageToSend)
        }
        const queuedMessage = this._sendQueue.getMessage()
        if (queuedMessage && queuedMessage.isResponseMessage(messageReceived)) {
            this._sendQueue.dequeue()
        }
        const result = messageReceived.command !== TOKEN_COMMAND
        return result
    }

    /**
     * Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback (supported: 'send')
     * @param {Send} callback function(...parameter)
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) {
        this._callbacks.on(event, callback)
    }

    /**
     * Handler called each tick to check if any message must or may be sent
     * @returns true, if any message has been sent.
     * @private
     */
    async _processTick () {
        this._tickCount++
        try {
            const message = this._tokenExchange.processStateNoMessage()
            if (message) {
                await this._sendMessage(message)
            }

            if (this._tokenExchange.maySend) {
                this._sendMessageFromQueue()

                // Prevent to send a message twice in the same tick.
                this._tokenExchange.maySend = false
            }
        } catch (err) {
            errorLog(err, DEBUG)
        }
    }

    /**
     * Starts the scheduler
     */
    async run () {
        this._closed = false
        while (!this._closed) {
            this._processTick()
            await delay(this._tickDelay)
        }
    }

    /**
     * Closes the scheduler
     */
    close () {
        this._closed = true
    }
}

module.exports = RS485Schedule
