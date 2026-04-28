/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Interface service to communicate with several arduino or similar microcontroller based on a serial interface
 */

'use strict'

const SerialDevice = require('./serialdevice')

/**
 * Creates a serial device object, if not already available
 * @param {Object} config serial device configuration settings
 * @param {SerialDevice} [serialDevice=null] serial device object
 */
function prepare (config, serialDevice = null) {
    if (!serialDevice) {
        serialDevice = new SerialDevice(config)
    }
    return serialDevice
}

module.exports = { prepare, SerialDevice }
