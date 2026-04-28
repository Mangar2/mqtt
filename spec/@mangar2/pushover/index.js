/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @Overview
 * Interface to the pushover service and App. It supports sending text messages and alerts to the
 * pushover app.
 */

'use strict'

const Pushover = require('./pushover')

/**
 * Prepares the object
 * @param {Object} config automation configuration
 * @param {Pushover} [pushover=null] existing automation class
 * @returns {Pushover} the prepared automation object
 */
function prepare (config, pushover = null) {
    if (!pushover) {
        pushover = new Pushover(config)
    }

    return pushover
}

module.exports = {
    Pushover,
    prepare
}
