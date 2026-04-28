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

const SerialMessage = require('./serialmessage')

const { errorLog, types } = require('@mangar2/utils')

/**
 * Parses serial data for objects
 * @private
 */
class ParseSerialData {
    constructor () {
        this._receivedDataAsString = ''
        this._decoder = new TextDecoder()
    }

    /**
     * Skips the noise from incoming data. Data must either start with a '[' or with a '{'.
     * Anything else is ignored.
     * @private
     */
    _skipNoise () {
        let index = 0
        for (; index < this._receivedDataAsString.length; index++) {
            const curChar = this._receivedDataAsString.charAt(index)
            if (curChar === '[' || curChar === '{') {
                break
            }
        }
        this._receivedDataAsString = this._receivedDataAsString.substr(index)
    }

    /**
     * Parses the incoming data for an object and returns it. Removes the object string from the received
     * data string. Does nothing, if no full object has been found
     * @param {string} closingBracket closing bracket of the object, either '}' or ']'
     * @returns {Object|Array} received object
     * @private
     */
    _parseObject (closingBracket) {
        const objectEnd = this._receivedDataAsString.indexOf(closingBracket)
        let receivedObject = null

        if (objectEnd > 0) {
            const objectString = this._receivedDataAsString.substr(0, objectEnd + 1)
            this._receivedDataAsString = this._receivedDataAsString.substr(objectEnd + 1)
            try {
                receivedObject = JSON.parse(objectString)
            } catch (err) {
                receivedObject = null
                errorLog(err)
            }
        }
        return receivedObject
    }

    /**
     * Parses incoming data for an object:
     * Removes any noise
     * Removes the object from the data, and returns the object if an object is found.
     * Do nothing, if no objects is found
     * @returns {Object|Array}
     * @throws {Error} fatal error, should not happen.
     * @private
     */
    _getObjectFromData () {
        let receivedObject = null
        this._skipNoise()
        if (this._receivedDataAsString.length > 0) {
            const startChar = this._receivedDataAsString.charAt(0)
            if (startChar === '[') {
                receivedObject = this._parseObject(']')
            } else if (startChar === '{') {
                receivedObject = this._parseObject('}')
            } else {
                throw Error('fatal error: "[" or "{" expected, but found ' + startChar)
            }
        }
        return receivedObject
    }

    /**
     * Converts a bit string in a value
     * @param {string} bitString string of bits (0/1) like 01100110
     * @returns {integer}
     */
    _bitStringToValue (bitString) {
        let result = 0
        for (let index = bitString.length - 1; index >= 0; index--) {
            result *= 2
            if (bitString.charAt(index) === '1') {
                result++
            }
        }
        return result
    }

    /**
     * Transformes a received object in a standard format
     * @param {Array|Object} receivedObject JSON decoded object received
     * @returns {SerialMessage|null} Serial message or null, if the data is not sufficiant to createa serial message
     */
    _transform (receivedObject) {
        let result = null
        const isObject = types.isObject(receivedObject)
        const isYahaArduinoFormat = isObject && receivedObject.S !== undefined
        const isFS20Format = isObject && receivedObject.Hauscode !== undefined

        if (types.isArray(receivedObject)) {
            const sender = receivedObject[0]
            const command = receivedObject[1]
            const interfaceName = command === 'switch' ? 'switch' : 'i2c'
            const value = command === 'switch' ? this._bitStringToValue(receivedObject[2]) : receivedObject[2]
            result = new SerialMessage(interfaceName, sender, null, command, value)
        } else if (isYahaArduinoFormat) {
            result = new SerialMessage('serial', receivedObject.S, receivedObject.R, receivedObject.K, receivedObject.V)
        } else if (isFS20Format) {
            const value = receivedObject.Befehl === 0 ? 'off' : 'on'
            result = new SerialMessage(
                'fs20',
                null,
                null,
                receivedObject.Hauscode + '/' + receivedObject.Adresse,
                value,
                '/set')
        }
        return result
    }

    /**
     * Transforms serial data to objects and returns it
     * @param {string} stream stream containing the data
     * @returns {SerialMessage|null} Serial message or null, if the data is not sufficiant to createa serial message
     */
    parse (stream) {
        
        this._receivedDataAsString += this._decoder.decode(stream)
        const receivedObject = this._getObjectFromData()
        const transformedObject = this._transform(receivedObject)
        return transformedObject
    }
}

module.exports = ParseSerialData
