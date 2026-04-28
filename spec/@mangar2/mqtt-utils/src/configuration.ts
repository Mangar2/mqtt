/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Provides a function to sanitize configuration file input and to provide default values
 */

'use strict'

import { deepMerge, StringIndexed } from '@mangar2/utils'
import  CheckInput from '@mangar2/checkinput'

/**
 * Sanitizes configuration input and merges it with default values, if not provided in configuration
 * @param {Object} configuration provided configuration (from configuration file), the function
 * changes this object directly, you may ignore the return value
 * @param {Object} defaultValues configuration subset with default values
 * @param {CheckInput} checkInput check function to sanitize the configuration.
 * Please set all attributes as mandatory, they get optional by providing default values
 * @returns {Object} merged configuration in provided configuration object
 * @example
 * const configuration = { one: 'one', two: 'not two' }
 * const defaultValues = { two: 'two', three: 3 }
 * const checkInput = new CheckInput({
 *     type: 'object',
 *     properties: {
 *         one: { type: 'string' },
 *         two: { type: 'string' },
 *         three: { type: 'number' }
 *     },
 *     required: ['one', 'two', 'trhee']
 * })
 * configuration = sanitize(configuration, defaultValues, checkInput)
 * // configuration is now { one: 'one', two: 'not two', three: 3 }
 */

export function sanitize (configuration: StringIndexed , defaultValues: StringIndexed, checkInput: CheckInput) {
    const result = deepMerge(configuration, defaultValues)
    checkInput.throwOnValidationError(result, 'Error in configuration parameter: ')
    return result
}

