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

const { SWITCH_ON, SWITCH_OFF } = require('./constants')

/**
 * Returns the number of the least significant bit of an integer
 * @param {integer} value
 * @returns {integer} -1, if no bit set, else the position of the bit set (starting with bit 0)
 */
function lsb (value) {
    let result = 0
    while ((value & 1) === 0 && value >= 1) {
        value /= 2
        result++
    }
    const noLSBFound = (value & 1) === 0
    if (noLSBFound) {
        result = -1
    }
    return result
}

/**
 * Calculates the switching string from a switch message
 * @param {SerialMessage} message message to convert
 * @param {number} message.value value used to identify the switch and the switching value
 */
function switchMessageToString (message) {
    let result = ''
    const switchValue = message.value
    const valueLSB = lsb(switchValue)
    if (valueLSB <= 8) {
        result = 's' + valueLSB
        if (switchValue & SWITCH_ON) {
            result += 'H'
        } else if (switchValue & SWITCH_OFF) {
            result += 'L'
        } else {
            result = ''
        }
    }
    return result
}

/**
 * Converts a serial message to a byte stream
 * @param {SerialMessage} message message to convert
 * @returns {string} string to transmit
 */
function serialMessageToString (message) {
    let result
    const commandArray = message.command.split('/')
    switch (message.interfaceName) {
    case 'switch':
        result = switchMessageToString(message)
        break
    case 'i2c':
        result = 'C' + message.receiver + message.command + message.value
        break
    case 'serial':
        result = JSON.stringify({ S: message.sender, R: message.receiver, C: message.command, V: message.value })
        break
    case 'fs20':
        result = 'G' + commandArray[1] + message.value
        break
    }
    return result
}

module.exports = { serialMessageToString }
