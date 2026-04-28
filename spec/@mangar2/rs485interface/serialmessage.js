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

const { toHexString } = require('./hexstring')
const { calcCrc16, calcParity } = require('./crc16')
const BITS_IN_BYTE = 8
const {
    MESSAGE_SIZE_V0,
    MESSAGE_SIZE_V1,
    TOKEN_COMMAND,
    ENABLE_SEND,
    REGISTRATION_INFO,
    REGISTRATION_REQUEST
} = require('./constants')

/**
 * Gets the local time in ISO string format
 * @private
 */
const getTimeAsISOString = () => {
    return (new Date()).toISOString()
}

/**
 * Message with command and value with a fixed size to be transported via. serial connections
 * @param {0|1} [version=1] version of the message. The version 0 uses a parity byte and the version 1
 * uses a two byte cyclic redundancy check value and includes the version in byte streams
 * @private
 */
class SerialMessage {
    constructor (version = 1) {
        this.sender = 0
        this.receiver = 0
        this.reply = false
        this.command = 0
        this.value = 0
        this.parity = 0
        this.crc16 = 0
        this.version = version
        this.length = version > 0 ? MESSAGE_SIZE_V1 : MESSAGE_SIZE_V0
        this._timestamp = getTimeAsISOString()
    }

    /**
     * Address of the device sending the message
     * @type {byte}
     */
    get sender () { return this._sender }
    set sender (sender) { this._sender = sender }

    /**
     * Address of the device receiving the message
     * @type {byte}
     */
    get receiver () { return this._receiver }
    set receiver (receiver) { this._receiver = receiver }

    /**
     * Flag if the message sender requests a reply
     * @type {boolean}
     */
    get reply () { return this._reply }
    set reply (reply) { this._reply = reply }

    /**
     * Command of the message
     * @type {char}
     */
    get command () { return this._command }
    set command (command) { this._command = command }

    /**
     * Value of the message
     * @type {word}
     */
    get value () { return this._value }
    set value (value) { this._value = value }

    /**
     * Parity byte (optional)
     * @type {byte}
     */
    get parity () { return this._parity }
    set parity (parity) { this._parity = parity }

    /**
     * Cyclic redundancy check code (16 bit), (optional)
     * @type {word}
     */
    get crc16 () { return this._crc16 }
    set crc16 (crc16) { this._crc16 = crc16 }

    /**
     * Length of the message in bytes
     * @type {integer}
     */
    get length () { return this._length }
    set length (length) { this._length = length }

    /**
     * Binary lenght of the message if transported over a serial connection
     * @type {integer}
     */
    get version () { return this._version }
    set version (version) { this._version = version }

    /**
     * Sets the value of the package from a high and a low order byte
     * @param {byte} valueHigh
     * @param {byte} valueLow
     * @private
     */
    _setValueHighLow (valueHigh, valueLow) {
        if (this.command === 'h' || this.command === 't' || this.command === 's') {
            this.value = valueHigh + (valueLow / 100)
        } else {
            this.value = valueHigh * 256 + valueLow
        }
    }

    /**
     * Sets the CRS16 of the message, if it matches the calculated CRC16
     * @param {byte} lowByte lower part of the CRC16 value
     * @param {byte} highByte higher part of the CRC16 value
     * @param {word} calculatedCRC16 calcuated CRC16 value
     * @private
     */
    _setCRC16 (lowByte, highByte, calculatedCRC16) {
        this.crc16 = (highByte << BITS_IN_BYTE) + lowByte
        if (this.crc16 !== calculatedCRC16) {
            throw Error('CRC does not match. Expected: ' +
                calculatedCRC16.toString(16) +
                ' Found: ' +
                this.crc16.toString(16))
        }
    }

    /**
     * Sets the length of the message
     * @param {Array} byteArray serial byte Array
     * @param {number} startIndex first element to concider
     * @private
     */
    _setLength (byteArray, startIndex) {
        switch (this.version) {
        case 0: this.length = MESSAGE_SIZE_V0; break
        case 1: this.length = byteArray[startIndex + 3]; break
        default: this.length = MESSAGE_SIZE_V0; break
        }
    }

    /**
     * Sets the message information from an array of bytes received from a serial port.
     * @param {Array} byteArray serial byte Array
     * @param {number} startIndex first element to concider
     * @throws {Error} error, if data does not match requirements
     * @private
     */
    _setV0 (byteArray, startIndex) {
        const calculatedParity = calcParity(byteArray, startIndex, MESSAGE_SIZE_V0 - 1)
        const receivedParity = byteArray[startIndex + 6]
        this.command = String.fromCharCode(byteArray[startIndex + 3])
        this._setValueHighLow(byteArray[startIndex + 4], byteArray[startIndex + 5])
        if (byteArray.length < startIndex + this.length) {
            throw Error('Insufficient data received')
        } else if (calculatedParity !== receivedParity) {
            throw Error('Parity does not match, expected: ' + calculatedParity + ' received: ' + receivedParity)
        }
    }

    /**
     * Sets the message information from an array of bytes received from a serial port.
     * @param {Array} byteArray
     * @param {number} startIndex first element to concider
     * @throws {Error} error, if data does not match requirements
     * @private
     */
    _setV1 (byteArray, startIndex) {
        const crc16 = calcCrc16(byteArray, startIndex, MESSAGE_SIZE_V1 - 2)
        if (byteArray.length < startIndex + this.length) {
            throw Error('Insufficient data received')
        } else {
            this.command = String.fromCharCode(byteArray[startIndex + 4])
            this._setValueHighLow(byteArray[startIndex + 5], byteArray[startIndex + 6])
            this._setCRC16(byteArray[startIndex + 7], byteArray[startIndex + 8], crc16)
        }
        if (this.length !== MESSAGE_SIZE_V1) {
            throw Error('Illegal message length, expected: ' + MESSAGE_SIZE_V1 + ' received: ' + this.length)
        }
    }

    /**
     * Sets the message information from an array of bytes received from a serial port.
     * @param {Array} byteArray
     * @param {number} startIndex first element to concider
     * @throws {Error} error, if byteArray does not match requirements
     */
    setFromByteArray (byteArray, startIndex) {
        this.sender = byteArray[startIndex]
        this.receiver = byteArray[startIndex + 1]
        this.reply = (byteArray[startIndex + 2] & 1) === 1
        this.version = byteArray[startIndex + 2] >> 1
        // Ensure to have a length value, even if an error is thrown later
        this._setLength(byteArray, startIndex)
        this.timestamp = getTimeAsISOString()

        if (this.sender > 127) {
            throw Error('Illegal sender address ' + this.sender)
        }
        if (this.receiver > 127) {
            throw Error('Illegal receiver address ' + this.receiver)
        }

        switch (this.version) {
        case 0: this._setV0(byteArray, startIndex); break
        case 1: this._setV1(byteArray, startIndex); break
        default:
            throw Error('Version not supported: ' + this.version)
        }
    }

    /**
     * Fills the remaining data of a version 0 message
     * @returns {Buffer} Buffer filled with message in bytecode
     * @private
     */
    _getByteArrayV0 () {
        const byteArray = Buffer.alloc(MESSAGE_SIZE_V0)
        const reply = this.reply ? 1 : 0

        byteArray[0] = this.sender
        byteArray[1] = this.receiver
        byteArray[2] = reply + (this.version << 1)

        byteArray[3] = this.command.charCodeAt(0)
        byteArray[4] = this.value / 256
        byteArray[5] = this.value % 256
        const parity = calcParity(byteArray, 0, MESSAGE_SIZE_V0 - 1)
        byteArray[6] = parity

        return byteArray
    }

    /**
     * Fills the remaining data of a version 0 message
     * @returns {Buffer} Buffer filled with message in bytecode
     * @private
     */
    _getByteArrayV1 () {
        const byteArray = Buffer.alloc(MESSAGE_SIZE_V1)
        const reply = this.reply ? 1 : 0

        byteArray[0] = this.sender
        byteArray[1] = this.receiver
        byteArray[2] = reply + (this.version << 1)

        byteArray[3] = MESSAGE_SIZE_V1
        byteArray[4] = this.command.charCodeAt(0)
        byteArray[5] = this.value / 256
        byteArray[6] = this.value % 256
        const crc = calcCrc16(byteArray, 0, 7)
        byteArray[7] = crc & 0xFF
        byteArray[8] = crc >> 8

        return byteArray
    }

    /**
     * Transforms the current message to an array of byte
     * @returns {Buffer} Buffer holding the message in bytecode
     */
    getByteArray () {
        let byteArray
        switch (this._version) {
        case 0: byteArray = this._getByteArrayV0(); break
        case 1: byteArray = this._getByteArrayV1(); break
        default:
            throw 'Unsupported message version ' + this.version
        }

        return byteArray
    }

    /**
     * Gets the value of the message for logging purpouses
     * @returns {string} value to log
     * @private
     */
    _getValueToLog () {
        let value = this.value
        if (this.command === TOKEN_COMMAND) {
            switch (this.value) {
            case ENABLE_SEND: value = 'enable send'; break
            case REGISTRATION_INFO: value = 'reg. info'; break
            case REGISTRATION_REQUEST: value = 'reg. request'; break
            }
        }
        return value
    }

    /**
     * Returns true, if the message is an internal message
     * @returns {boolean} true, if internal
     */
    isInternal () {
        return this.command === TOKEN_COMMAND
    }

    /**
     * Gets a string to log to a debug output
     * @returns {string} string to log
     */
    getLoggingInfo () {
        const now = new Date()
        const reply = this.reply ? 1 : 0
        let result = now.toLocaleTimeString() + ' ' +
            this.sender + ' => ' + this.receiver + ' (r:' + reply + '): ' +
            this.command + ' = ' + this._getValueToLog()
        while (result.length < 42) {
            result += ' '
        }
        const byteArray = this.getByteArray()
        result += toHexString(byteArray, 0, this.length)
        return result
    }

    /**
     * Returns true, if a message is a response message
     * @param {Message} message original message sent to a device
     * @returns {boolean} true, if current message is a response message to the message
     * passed as parameter
     */
    isResponseMessage (message) {
        const result =
            message !== undefined &&
            message.sender === this.receiver &&
            message.receiver === this.sender &&
            message.command === this.command &&
            message.value === this.value
        return result
    }
}

module.exports = SerialMessage
