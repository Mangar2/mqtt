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
 * Start value for the crc16 calculation
 * @private
 */
const CCITT_CRC16_START_VALUE = 0xFFFF

/**
 * CRC16 calcuation polynom
 * @private
 */
const CCITT_CRC16_POLYNOM = 0x1021

/**
 * Amount of bits in one byte
 * @private
 */
const BITS_IN_BYTE = 8

/**
 * Shifts the least significant byte to the most significant byte position in a two byte
 * variable
 * @param {word} data two byte number
 * @returns {word}
 * @private
 */
const LSByteToMSByte = (data) => {
    return (data << BITS_IN_BYTE) & 0xFF00
}

/**
 * Checks, if the most significant byte is set
 * @param {word} data two byte number
 * @returns {boolean} true, if the most significant byte is set
 * @private
 */
const isMSBSet = (data) => {
    return (data & 0x8000) !== 0
}

/**
 * Calculates a 16 bit cyclic redundancy check value from an array of byte from
 * startIndex to startIndex + lenght - 1
 * @param {Array} byteArray array of bytes
 * @param {integer} startIndex start position for crc16 calculation
 * @param {integer} length amount of elements to include in the calculation
 * @returns {integer} 16 bit numeric value
 * @private
 */
const calcCrc16 = (byteArray, startIndex, length) => {
    let crc = CCITT_CRC16_START_VALUE
    let bufferIndex
    let shiftLoop
    for (bufferIndex = startIndex; bufferIndex < startIndex + length; bufferIndex++) {
        crc ^= LSByteToMSByte(byteArray[bufferIndex])
        for (shiftLoop = 0; shiftLoop < 8; shiftLoop++) {
            if (isMSBSet(crc)) {
                crc = (crc << 1) ^ CCITT_CRC16_POLYNOM
            } else {
                crc <<= 1
            }
        }
    }
    return crc & 0xFFFF
}

const calcParity = (byteArray, startIndex, length) => {
    let i
    let parity = 0

    if (byteArray.length >= length) {
        for (i = 0; i < length; i++) {
            parity = parity ^ byteArray[i + startIndex]
        }
    }

    return parity
}

module.exports = { calcCrc16, calcParity }
