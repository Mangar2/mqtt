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
 * @description checks, that variable and compare are numbers
 * @param {any} variable variable
 * @param {any} compare value to compare
 * @throws {Error} if variable or compare is not a number or a date
 */
function checkForNumber (variable, compare) {
    if (isNaN(variable) && types.getType(variable) !== 'Date') {
        throw Error('first operand value "' + variable + '" is not a number')
    }
    if (isNaN(compare) && types.getType(compare) !== 'Date') {
        throw Error('second operand value "' + compare + '" is not a number')
    }
}

/**
 * @private
 * @description
 * Compares two elements, converts Date with "getTime"
 * @param {anything} operand1 first element to compare
 * @param {anything} operand2 second element to compare
 */
function isEqual (operand1, operand2) {
    if (types.isDate(operand1) && types.isDate(operand2)) {
        return operand1.getTime() === operand2.getTime()
    } else {
        return operand1 === operand2
    }
}

/**
 * @private
 * @description Compares to values according an operator
 * @param {string} operator compare operator ("<", ">", "above", "below", "greaterOrEqual", "lessOrEqual", "equal")
 * @param {Object} operand1 {value, variable} first element to compare with
 * @param {Object} operand2 {value, variable} second element to compare with
 * @throws {Error} on any error in operator or operands
 * @returns {EvalResult} result of the operation
 */
module.exports = (operator, operand1, operand2) => {
    const operand1Value = operand1.value
    const operand2Value = operand2.value
    const result = new EvalResult(false, operand1.reason)
    if (!types.isString(operator)) {
        throw Error('operator must be of type string "' + operator + '" provided')
    }
    if (operand1Value !== undefined && operand2Value !== undefined) {
        switch (operator.toLowerCase().trim()) {
        case '=':
        case 'equal':
        case 'equals':
            result.value = isEqual(operand1Value, operand2Value)
            result.reason += result.value ? ' is equal to ' : ' is different to '
            result.reason += operand2.reason
            break
        case '!=':
        case '<>':
        case 'not equal':
        case 'not equals':
            result.value = !isEqual(operand1Value, operand2Value)
            result.reason += result.value ? ' is not equal to ' : ' is not different to '
            result.reason += operand2.reason
            break
        case '<':
        case 'below':
        case 'less':
            checkForNumber(operand1Value, operand2Value)
            result.value = operand1Value < operand2Value
            result.reason += result.value ? ' is below ' : ' is not below '
            result.reason += operand2.reason
            break
        case '>':
        case 'above':
        case 'greater':
            checkForNumber(operand1Value, operand2Value)
            result.value = operand1Value > operand2Value
            result.reason += result.value ? ' is above ' : ' is not above '
            result.reason += operand2.reason
            break
        case '<=':
        case 'below or equal':
        case 'less or equal':
            checkForNumber(operand1Value, operand2Value)
            result.value = operand1Value <= operand2Value
            result.reason += result.value ? ' is below or equal ' : ' is not below or equal '
            result.reason += operand2.reason
            break
        case '>=':
        case 'above or equal':
        case 'greater or equal':
            checkForNumber(operand1Value, operand2Value)
            result.value = operand1Value >= operand2Value
            result.reason += result.value ? ' is above or equal ' : ' is not above or equal '
            result.reason += operand2.reason
            break
        default:
            throw Error('unknown operator ' + operator)
        }
    }
    return result
}
