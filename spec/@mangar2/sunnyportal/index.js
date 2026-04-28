/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @Overview
 * Reads data from sunnyportal from SNA
 */

'use strict'

const SunnyPortal = require('./sunnyportal')

/**
 * Prepares the object
 * @param {Object} config automation configuration
 * @param {SunnyPortal} [sunnyportal=null] existing automation class
 * @returns {SunnyPortal} the prepared automation object
 */
function prepare (config, sunnyportal = null) {
    if (!sunnyportal) {
        sunnyportal = new SunnyPortal(config)
    }

    return sunnyportal
}

module.exports = {
    SunnyPortal,
    prepare
}
