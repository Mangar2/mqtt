/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Interface service to communicate with several arduino or similar microcontroller based on a RS485 bus
 * The underlying protocol is token based. Tokens are send from one arduino to the other. The arduino having
 * the token may send. Data is send in an internal message format. The message contains:
 * - Byte: Sender address
 * - Byte: Receiver address
 * - Byte: Reply (bit 1) and message format version (bit 2..8)
 * - Byte: Message length
 * - Byte: Command to handle
 * - Word: (2 bytes) value, usual positive integer
 * - Word: CRC16 for error handling
 */

'use strict'

const RS485Interface = require('./rs485interface')

/**
 * Creates a new rs485Interface object, if not already existing
 * @param {Object} config configuration for the rs485 interface
 * @param {RS485Interface} [rs485Interface=null] already existing interface object
 */
function prepare (config, rs485Interface = null) {
    if (!rs485Interface) {
        rs485Interface = new RS485Interface(config)
    }
    return rs485Interface
}

module.exports = { prepare, RS485Interface }
