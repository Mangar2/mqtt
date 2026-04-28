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
 * Creates a debug information as string from a
 * @param {Array} byteArray
 * @param {number} startIndex first element to concider
 * @param {number} messageSize length of the message
 * @returns {string} string showing legth and hex code of the byteArray
 * @private
 */
const toHexString = (byteArray, startIndex, messageSize) => {
    if (byteArray === undefined) {
        return ''
    }
    const length = byteArray.length
    let log = '([' + length + '] '

    for (let i = 0; i < Math.min(length - startIndex, messageSize); i++) {
        let hex = byteArray[i + startIndex].toString(16)
        if (hex.length < 2) {
            hex = '0' + hex
        }
        log += ' ' + hex
    }
    log += ')'
    return log
}

module.exports = { toHexString }
