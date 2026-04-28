/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Provides an object to connect openzwave to the yaha mqtt-like broker
 */

'use strict'

const Zwave = require('./zwaveservice')

/**
 * Instantiates exactly one zwave object
 * @param {Object} config automation configuration
 * @param {Zwave} [zwave=null] existing zwave class
 * @returns {Zwave} the prepared zwave object
 */
function prepare (config, zwave = null) {
    if (!zwave) {
        zwave = new Zwave(config)
    }

    zwave.setDeviceConfiguration(config)

    return zwave
}

module.exports = {
    Zwave,
    prepare
}
