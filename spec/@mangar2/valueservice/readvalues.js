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

const { types } = require('@mangar2/utils')
const fs = require('fs')

/**
 * @private
 * @description Get/reads variable with values from files
 * @param {string} filename name of the file storing the values
 * @returns {Object} list of values
 */
function readValues (filename) {
    let result = {}
    if (types.isString(filename)) {
        result = JSON.parse(fs.readFileSync(filename))
    }
    return result
}


module.exports = {
    readValues
}
