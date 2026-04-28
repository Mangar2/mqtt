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

const { sunrise, sunset } = require('@mangar2/sun')

/**
 * @description Manages a set of variables
 * @param {Date} [date=now] current date
 * @param {number} [longitude = 51.476852] geographical longitued poistion of the automation target in degrees
 * @param {number} [latitude = -0.000500] geographical latitued poistion of the automation target in degrees
 */
class Variables {
    constructor (date, longitude, latitude) {
        this._longitude = longitude
        this._latitude = latitude
        this._variables = {}
        this._type = {}
        this.calculateInternalVariables(date)
    }

    /**
     * Adds a variable (coming from the service itself) to the variable list
     * @param {string} name name of the variabl
     * @param {any} value value of the variable
     */
    _addInternVariable(name, value) {
        this._variables[name] = value
        this._type[name] = 'intern'
    }

    /**
     * Calculates all internal variables (usually after a date change)
     */
    calculateInternalVariables(date) {
        this._addInternVariable('/time', date)
        this._addInternVariable('/weekday', date.getDay())
        this._addInternVariable('/sunrise', sunrise(this._longitude, this._latitude, date))
        this._addInternVariable('/civildawn', sunrise(this._longitude, this._latitude, date, 96))
        this._addInternVariable('/nauticaldawn', sunrise(this._longitude, this._latitude, date, 102))
        this._addInternVariable('/astronomicaldawn', sunrise(this._longitude, this._latitude, date, 108))
        this._addInternVariable('/sunset', sunset(this._longitude, this._latitude, date))
        this._addInternVariable('/civildusk', sunset(this._longitude, this._latitude, date, 96))
        this._addInternVariable('/nauticaldusk', sunset(this._longitude, this._latitude, date, 102))
        this._addInternVariable('/astronomicaldusk', sunset(this._longitude, this._latitude, date, 108))
    }

    /**
     * Returns true, if the variable is an external variable required from outside
     * @param {string} name name of the variable
     * @returns {boolean} true, if the variable is external (send to the service)
     */
    isExternVariable(name) {
        return (this._type[name] !== 'intern')
    }

    /**
     * Returns the value of the variable
     * @param {string} name name of the variable
     * @returns {any} value of the variable
     */
    getValue(name) {
        return this._variables[name]
    }

    /**
     * Sets the value of an existing variable
     * @param {string} name name of the variable
     * @param {any} value value of the variable
     */
    setValue(name, value) {
        this._variables[name] = value
    }

    /**
     * Gets a key/value map of all variables
     * @returns {{key:string, value:any}} key/value map of all variables
     */
    getAllVariables() {
        return this._variables
    }
}

module.exports = Variables