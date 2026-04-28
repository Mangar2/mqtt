/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Provides a JSON based term evaluator to calculate decisions based on configuration files
 * The syntax of the JSON-terms is derived from Lisp.
 */

'use strict'

const types = require('@mangar2/types')
const Callbacks = require('@mangar2/callbacks')
const { isTimeOfDayString, timeOfDayStringToDate } = require('@mangar2/time')
const EvalResult = require('./evalresult')
const compare = require('./compare')
const calculate = require('./calculate')

/**
 * Creates a value class providing the ability to evalutate a json based value. Additionally it explains the
 * value in a juman readable string.
 * Supported operations are
 * - ["or", t1, t2, ... ]
 * - ["and", t1, t2, ...]
 * - [operator, t1, t2] with operators '>', '<', '>=', '<=', '!=', '='
 * - ["+/-", t1, t2]
 * - ["if", t1, then, else]
 * - ["switch", t1, {map}] where map is an array of variable value maps. Example ["switch", 1, {1:"a", 2:"b"}] results in 'a'
 * - ["function", ...parameter] where function is registrated by a call to 'on'
 * @param {Object} [variables={}] list of known variables and values
 * @throws {Error} if variables are not an object
 * @example
 * const variables = {
 *    'a/b': 1,
 *    'b/c': 2,
 *    'system/presence': 'awake'
 * }
 * const value = new Decision(variables)
 * const result = value.decide(
 *      [
 *           'and',
 *           ['or', ['<', 'a/b', 10], ['<', 'b/c', 2]],
 *           ['or', ['>=', 'a/b', 10], ['>=', 'b/c', 2]]
 *       ]
 * )
 * // returns result.value = true and result.reason = 'a/b (1) is < 10 and b/c (2) is >= 2'
 */
class Decision {
    constructor (variables = {}) {
        if (!types.isObject(variables)) {
            throw Error('variables parameter must be of type object')
        }
        this._callbacks = new Callbacks()
        this.variables = variables
        this._usedVariables = {}
    }

    /**
     * @private
     * @description Set the environment variables
     * @param {Object} variables list of known variables and values
     */
    set variables (variables) {
        this._variables = {}
        for (const variable in variables) {
            this._variables[variable.toLowerCase()] = variables[variable]
        }
    }

    /**
     * @description Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback
     * @param {function} callback(parameter)
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) {
        this._callbacks.on(event, callback)
    }

    /**
     * @private
     * @description Generates a string containing all undefined variables
     * @returns {string} empty string, if no variable is undefined, else string listing all undefined variables
     */
    _genUndefinedVariablesString () {
        let result = ''
        let spacer = 'false, undefined variables: '
        for (const name in this._usedVariables) {
            if (this._usedVariables[name] === undefined) {
                result = result + spacer + name
                spacer = ', '
            }
        }
        return result
    }

    /**
     * @description Calculates a value based on a json based json tree
     * @param {Object} jsonTree json structured boolean logic
     * @throws {Error} on any error in the jsonTree
     * @returns {EvalResult} result of the evaluation
     */
    eval (jsonTree) {
        this._usedVariables = {}
        const result = this._evalNodeRec(jsonTree)
        const undefinedVariablesString = this._genUndefinedVariablesString()
        if (undefinedVariablesString !== '') {
            result.value = false
            result.reason = undefinedVariablesString
        }
        result.variables = this._usedVariables
        return result
    }

    /**
     * @private
     * @description Evaluates a json tree recursively
     * @param {Object} jsonNode node in a json tree
     * @throws {Error} on any error in the jsonNode
     * @returns {EvalResult} result of the evaluation
     */
    _evalNodeRec (jsonNode) {
        let result = new EvalResult(false, 'error')
        if (types.isArray(jsonNode)) {
            if (jsonNode.length === 0) {
                throw Error('empty arrays are not allowed')
            }
            if (jsonNode.length === 1) {
                result.value = jsonNode[0]
                result.result = jsonNode[0]
            } else {
                const copyNode = [...jsonNode]
                const operator = copyNode.shift()
                switch (operator.toLowerCase()) {
                case 'and': result = this._evalAnd(copyNode); break
                case 'or': result = this._evalOr(copyNode); break
                case 'switch': result = this._evalSwitch(copyNode); break
                case 'if': result = this._evalIf(copyNode); break
                case 'call': result = this._evalFunction(copyNode); break
                case '-':
                case '+': result = this._evalOperator(operator, copyNode); break
                default: result = this._evalCompOperator(operator, copyNode)
                }
            }
        } else if (types.isNumber(jsonNode)) {
            result = new EvalResult(jsonNode, jsonNode)
        } else if (types.isString(jsonNode)) {
            result = this._evalString(jsonNode)
        } else if (jsonNode === true) {
            result.value = true
            result.reason = 'constant true'
        } else if (jsonNode === false) {
            result.value = false
            result.reason = 'constant false'
        } else {
            throw Error('Array, Number, Boolean or String expected: ' + JSON.stringify(jsonNode))
        }
        return result
    }

    /**
     * @private
     * @description Evaluates function node
     * @param {Object} jsonNode variable node of the json tree
     * @throws {Error} on any error in the jsonNode
     * @returns {EvalResult} result of the evaluation
     */
    _evalFunction (jsonNode) {
        const result = new EvalResult(false)
        const name = jsonNode[0]
        jsonNode.shift()
        if (!types.isString(name)) {
            throw Error('Called function names must be of type string: ' + JSON.stringify(name))
        }
        if (!this._callbacks.hasCallback(name)) {
            throw Error('Called unknown function: ' + name)
        }
        result.value = this._callbacks.invokeCallback(name, ...jsonNode)
        result.reason = name + '(' + jsonNode.join(', ') + ') returned ' + result.value
        if (!types.isBoolean(result.value)) {
            result.value = false
        }
        return result
    }

    /**
     * Converts any value from an ISO formatted date string to a date object
     * If it is not an ISO formatted date string, the original value will be returned
     * @param {any} value any value 
     * @returns {Date|any} a Date object or the original value
     */
    _ISOStringToDate (value) {
        // Transform strings in date ISO Format to dates
        try {
            const dateParsed = new Date(Date.parse(value))
            if (dateParsed.toISOString() === value) {
                value = dateParsed
            } 
        } catch {
            // Do nothing as the value is not a date
        }
        return value
    }

    /**
     * @private
     * @description Evaluates a node containing a string - either a variable or a string
     * It will convert variables holding a string in ISO date format to a date.
     * @param {string} string variable name or string having a value
     * @returns {EvalResult} value of the string or variable
     */
    _evalString (string) {
        const result = new EvalResult(string, string)
        // Variables must include a '/' (else it is not a variable but only a string)
        if (string.includes('/')) {
            let value = this._variables[string.toLowerCase()]
            // Do not modify the variable itself in the calculation
            if (types.getType(value) === 'Date') {
                value = new Date(value)
            }
            value = this._ISOStringToDate(value)
            result.value = (value !== undefined ? value : false)
            this._reportVariable(value, string)
            const explainVariable = types.isDate(value) ? value.toLocaleTimeString() : value
            result.reason = string + ' (' + explainVariable + ')'
        } 
        if (isTimeOfDayString(result.value)) {
            result.value = timeOfDayStringToDate(result.value)
        }
        return result
    }

    /**
     * @private
     * @description Evaluates a node containing a compare operator
     * @param {string} operator operator string
     * @param {Array} jsonNode operands
     * @throws {Error} on any error in the jsonNode
     * @returns {EvalResult} result of the evaluation
     */
    _evalCompOperator (operator, jsonNode) {
        let result = new EvalResult(false)
        if (jsonNode.length !== 2) {
            throw Error('term array needs three elements' + JSON.stringify([operator, ...jsonNode]))
        }
        const first = this._evalNodeRec(jsonNode[0])
        const second = this._evalNodeRec(jsonNode[1])
        result = compare(operator, first, second)

        return result
    }

    /**
     * @private
     * @description Evaluates a node containing an operator (+/-)
     * @param {string} operator operator string
     * @param {Array} jsonNode operands
     * @throws {Error} on any error in the jsonNode
     * @returns {EvalResult} result of the evaluation
     */
    _evalOperator (operator, jsonNode) {
        if (jsonNode.length !== 2) {
            throw Error('operator array needs exactly three elements' + JSON.stringify(jsonNode))
        }
        const operand1 = this._evalNodeRec(jsonNode[0])
        const operand2 = this._evalNodeRec(jsonNode[1])
        if (!types.isNumber(Number(operand2.value))) {
            throw Error(operand2.value + ' is not a number (' + JSON.stringify(jsonNode) + ')')
        }
        if (!types.isNumber(operand1.value) && !types.isDate(operand1.value)) {
            throw Error(operand1.value + ' is not a number or a date (' + JSON.stringify(jsonNode) + ')')
        }
        const result = calculate(operator, operand1, operand2)

        return result
    }

    /**
     * @private
     * @description calulates an and operation
     * @param {Object} jsonNode And node of the json tree
     * @returns {EvalResult} result of the evaluation
     */
    _evalAnd (jsonNode) {
        const result = new EvalResult(true)
        for (const element of jsonNode) {
            const nodeResult = this._evalNodeRec(element)
            if (result.value) {
                result.addAnd(nodeResult)
            }
        }
        return result
    }

    /**
     * @private
     * @description calulates an or operation
     * @param {Object} jsonNode node of the json tree
     * @returns {EvalResult} result of the evaluation
     */
    _evalOr (jsonNode) {
        const result = new EvalResult(false)
        for (const element of jsonNode) {
            const nodeResult = this._evalNodeRec(element)
            if (!result.value) {
                result.addOr(nodeResult)
            }
        }
        return result
    }

    /**
     * @private
     * @description calulates a case expression
     * @param {Object} jsonNode node of the json tree
     * @throws {Error} on any error in the jsonNode
     * @returns {EvalResult} result of the evaluation
     */
    _evalSwitch (jsonNode) {
        if (jsonNode.length !== 2) {
            throw Error('case construct needs exactly three elements' + JSON.stringify(jsonNode))
        }
        const result = new EvalResult(false)
        const map = jsonNode[1]
        const expression = this._evalNodeRec(jsonNode[0])
        let mapped = map[expression.value]
        if (mapped === undefined && map.default) {
            mapped = map.default
        }
        if (mapped === undefined) {
            result.value = false
        } else {
            result.value = this._evalNodeRec(mapped).value
        }
        result.reason = expression.reason + ' mapped to ' + result.value
        return result
    }

    /**
     * @private
     * @description calulates a case expression
     * @param {Object} jsonNode node of the json tree
     * @throws {Error} on any error in the jsonNode
     * @returns {EvalResult} result of the evaluation
     */
    _evalIf (jsonNode) {
        if (jsonNode.length < 3 && jsonNode.length > 4) {
            throw Error('If needs three or four elements' + JSON.stringify(jsonNode))
        }
        let result
        const expression = this._evalNodeRec(jsonNode[0])
        if (expression.value) {
            result = this._evalNodeRec(jsonNode[1])
            result.reason = 'if ' + expression.reason + ' then ' + result.reason
        } else {
            if (jsonNode[2] === undefined) {
                result = new EvalResult(false, 'false')
            } else {
                result = this._evalNodeRec(jsonNode[2])
            }
            result.reason = 'if ' + expression.reason + ': ' + result.reason
        }
        return result
    }

    /**
     * @private
     * @description Reports missing variables and once the variable is found
     * @param {any} variableValue value of the variable
     * @param {string} variableName name of the variable
     */
    _reportVariable (variableValue, variableName) {
        this._usedVariables[variableName] = variableValue
    }
}

module.exports = Decision
