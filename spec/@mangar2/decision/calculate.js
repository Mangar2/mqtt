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

const types = require('@mangar2/types')
const EvalResult = require('./evalresult')

/**
 * @private
 * @description Adds or subtracts two numbers or a number (minutes) from a date
 * @param {string} operator operator symbol '+' or '-'
 * @param {number|date} operand1 first operand
 * @param {number} operand2 second operand
 * @returns {EvalResult} result of the calculation
 */
module.exports = (operator, operand1, operand2) => {
    const result = new EvalResult(operand1.value)
    let valueStr = ''
    if (types.getType(operand1.value) === 'Date') {
        if (operator === '-') {
            result.value.setMinutes(result.value.getMinutes() - operand2.value)
        } else if (operator === '+') {
            result.value.setMinutes(result.value.getMinutes() + operand2.value)
        }
        valueStr = result.value.toLocaleTimeString()
    } else if (types.isNumber(operand1.value)) {
        if (operator === '-') {
            result.value -= operand2.value
        } else if (operator === '+') {
            result.value += operand2.value
        }
        valueStr = result.value
    }
    result.reason = valueStr + ' (' + operand1.reason + ' ' + operator + ' ' + operand2.reason + ')'
    return result
}
