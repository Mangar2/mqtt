/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Interface to the opensense map service. Use it to send data to opensense map.

 */

'use strict'

const OpensenseMap = require('./opensensemap')

/**
 * Prepares the object
 * @param {Object} config automation configuration
 * @param {OpensenseMap} [opensenseMap=null] existing automation class
 * @returns {OpensenseMap} the prepared automation object
 */
function prepare (config, opensenseMap) {
    if (!opensenseMap) {
        opensenseMap = new OpensenseMap(config)
    }

    return opensenseMap
}

module.exports = {
    OpensenseMap,
    prepare
}
