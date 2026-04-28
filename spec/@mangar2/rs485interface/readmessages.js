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

const { types } = require('@mangar2/utils')
const { toHexString } = require('./hexstring')
const SerialMessage = require('./serialmessage')
const {
    MAX_ADDRESS
} = require('./constants')

/**
 * @typedef ReadResult
 * @property {number} startIndex position of the next message in the input stream
 * @property {SerialMessage|null} message serial message read
 * @property {string} hex read data in hexadezimal values
 * @property {string|undefined} error error message
 * @private
 */

class ReadMessage {
    /**
     * Detects noise (usually received '0') and skips it
     * @param {Array} byteArray array containing the read bytes
     * @param {number} startIndex first element to consider
     * @returns {number} new startIndex
     * @private
     */
    _skipNoise (byteArray, startIndex) {
        // The RS485 USB Adapter gets zeros from time to time, if nobody is sending.
        // Messages coming in starts with the sender address that may neither be zero nor > MAX_ADDRESS
        while (byteArray.length > startIndex &&
            (byteArray[startIndex] === 0 || byteArray[startIndex] > MAX_ADDRESS)) {
            startIndex++
        }
        return startIndex
    }

    /**
     * Sets the package content from an array of bytes
     * @param {Array} byteArray array containing the read bytes
     * @param {number} startIndex first element to consider
     * @returns {ReadResult} mesage read
     * @private
     */
    _readMessage (byteArray, startIndex) {
        startIndex = this._skipNoise(byteArray, startIndex)
        let result
        const message = new SerialMessage()
        const hasRightType = types.getType(byteArray) === 'Uint8Array'
        try {
            if (hasRightType && byteArray.length > startIndex) {
                message.setFromByteArray(byteArray, startIndex)
                const hex = toHexString(byteArray, startIndex, message.length)
                startIndex += message.length
                result = { startIndex, message, hex, error: '' }
            }
        } catch (err) {
            const hex = toHexString(byteArray, startIndex, byteArray.length - startIndex)
            const error = err.message
            startIndex += message.length
            result = { startIndex, message: null, hex, error }
        }
        return result
    }

    /**
     * Reads messages from a serial stream and returns them in a message array
     * @param {Array} byteArray array of received bytes
     * @returns {ReadResult[]} array of read messages
     */
    read (byteArray) {
        let startIndex = 0
        let messageRead
        const result = []
        messageRead = this._readMessage(byteArray, startIndex)
        while (messageRead) {
            result.push(messageRead)
            startIndex = messageRead.startIndex
            messageRead = this._readMessage(byteArray, startIndex)
        }

        return result
    }
}

module.exports = ReadMessage
