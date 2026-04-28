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

/**
 * @description Creates a result object with a "value" and a "reason" and a list of used variable: value pairs
 * @param {Date|string|number|boolean} value evaluation value
 * @param {string} reason value reason in human readable form
 */
class EvalResult {
    constructor (value, reason = '') {
        this.value = value
        this.reason = reason
        this.variables = {}
    }

    /**
     * Value of the result
     * @type {Date|string|number|boolean}
     */
    get value () { return this._value }
    set value (value) { this._value = value }

    /**
     * Reason explaining the result
     * @type {string}
     */
    get reason () { return this._reason }
    set reason (reason) { this._reason = reason }

    /**
     * List of missing variables found
     * @type {string[]}
     */
    get variables () { return this._variables }
    set variables (variables) { this._variables = variables }

    /**
     * @private
     * @description
     * Adds the result of an And node evaluation
     * @param {EvalResult} nodeResult result of a node evaluation
     */
    addAnd (nodeResult) {
        this.value = this.value && nodeResult.value
        if (this.value) {
            this.reason = this.reason === '' ? nodeResult.reason : this.reason + ' and ' + nodeResult.reason
        } else {
            this.reason = nodeResult.reason
        }
    }

    /**
     * @private
     * @description
     * Adds the result of an Or node evaluation
     * @param {EvalResult} nodeResult result of a node evaluation
     */
    addOr (nodeResult) {
        this.value = this.value || nodeResult.value
        if (this.value) {
            this.reason = nodeResult.reason
        } else {
            this.reason = this.reason === '' ? nodeResult.reason : this.reason + ' or ' + nodeResult.reason
        }
    }
}

module.exports = EvalResult
