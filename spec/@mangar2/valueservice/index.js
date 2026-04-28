/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2022 Volker Böhm
 * @overview
 * Provides a service providing variables for values
 */

'use strict'

const ValueService = require('@mangar2/valueservice/valueService')
const { readValues } = require('./readvalues')

/**
 * Prepares the values
 * @param {Object} config values configuration
 * @param {Values} [values=null] existing values class
 * @returns {Values} the prepared values object
 */
function prepare (config, values = null) {
    if (!values) {
        values = new ValueService(config)
    }

    const valueSetting = readValues(config.valuesFileName)
    values.setVariables(valueSetting)

    return values
}

module.exports = {
    ValueService,
    prepare
}
