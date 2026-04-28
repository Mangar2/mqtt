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
 * Creates a serial message
 * @param {string} interfaceName name of the interface used/to use
 * @param {string|null} sender sender identifier
 * @param {string|null} receiver receiver identifier
 * @param {string} command message command
 * @param {string|number} value message value
 * @param {string} [action=''] additional action, supported '/set'
 * @private
 */
class SerialMessage {
    constructor (interfaceName, sender, receiver, command, value, action = '') {
        this.interfaceName = interfaceName
        this.sender = sender
        this.receiver = receiver
        this.command = command
        this.value = value
        this.action = action
    }

    toString () {
        return 'Interface: ' + this.interfaceName +
            ' Sender: ' + this.sender +
            ' Receiver: ' + this.receiver +
            ' Command: ' + this.command +
            ' Value: ' + this.value
    }

    /**
     * interface name identifier
     * @type {string}
     */
    get interfaceName () { return this._interfaceName }
    set interfaceName (interfaceName) { this._interfaceName = interfaceName }

    /**
     * sender identifier
     * @type {string}
     */
    get sender () { return this._sender }
    set sender (sender) { this._sender = sender }

    /**
     * receiver identifier
     * @type {string}
     */
    get receiver () { return this._receiver }
    set receiver (receiver) { this._receiver = receiver }

    /**
     * message command
     * @type {string}
     */
    get command () { return this._command }
    set command (command) { this._command = command }

    /**
     * message value
     * @type {number|string}
     */
    get value () { return this._value }
    set value (value) { this._value = value }

    /**
     * message action
     * @type {string}
     */
    get action () { return this._action }
    set action (action) { this._action = action }
}

module.exports = SerialMessage
