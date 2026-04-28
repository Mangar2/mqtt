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
 * @private
 * @description
 * Creates a new result object for a rule check
 * @param {boolean} check result of the check
 * @param {string} [reason=''] check result explained
 */
class CheckResult {
    constructor (check, reason = '') {
        this.check = check
        this.reason = reason
    }

    /**
     * @description
     * Gets the check result
     * @returns {boolean}
     */
    get check () { return this._check }

    /**
     * @description
     * Sets the check result
     * @param {boolean}  check
     */
    set check (check) { this._check = check }

    /**
     * @description
     * Gets the messages
     * @returns {Object|string}
     */
    get reason () { return this._reason }

    /**
     * @description
     * Sets the messages
     * @param {Object|string} reason
     */
    set reason (reason) { this._reason = reason }

    /**
     * @description
     * Adds another check-result with and logic
     * @param {boolean} check result of the check
     * @param {string} [reason=''] check result explained
     */
    andCheck (check, reason = '') {
        this.check = check && this.check
        if (this.check) {
            const spacer = this.reason === '' || reason === '' ? '' : ' and '
            this.reason = this.reason + spacer + reason
        } else {
            this.reason = ''
        }
    }

    /**
     * @description
     * Adds another check-result with or logic
     * @param {CheckResult} checkResult a second check result
     */
    orCheck (checkResult) {
        if (this.check === false) {
            this.setCheck(checkResult.check, checkResult.reason)
        }
    }

    /**
     * @description
     * Sets the check falues
     * @param {boolean} check result of the check
     * @param {string} [reason=''] check result explained
     */
    setCheck (check, reason = '') {
        this.check = check
        this.reason = reason
    }
}

module.exports = CheckResult
