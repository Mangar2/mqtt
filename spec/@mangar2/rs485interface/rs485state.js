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

const {
    STATE_UNKNOWN,
    STATE_REBOOT,
    STATE_SINGLE,
    STATE_UNREGISTERED,
    STATE_REGISTERED,
    STATE_UNCHANGED,
    ENABLE_SEND,
    REGISTRATION_INFO,
    REGISTRATION_REQUEST,
    STATE_CHANGED
} = require('./constants')

const BROADCAST_ADDRESS = 0

const MAX_WAIT_TIMER = 100
const TIMER_SMALL_PERIOD = 3
const TIMER_LARGE_PERIOD = 7
const TIMER_LOOP = TIMER_SMALL_PERIOD + TIMER_LARGE_PERIOD
const TIMEOUT_NO_ENABLE_SEND = 4 * TIMER_LOOP

const LOOP_TIMEOUT = 10
const LOOP_START = 11
const LOOP_SHORT_BREAK = 12
const LOOP_LONG_BREAK = 13

/**
 * Creates a class managing the rs485 token based communication state
 * @private
 */
class RS485State {
    constructor () {
        this._state = STATE_UNKNOWN
        this._timer = 0
        this._lastEnableSend = 0
        this.rightSibling = null
        this.leftmostSibling = null
        this.maySend = false
        this.trace = false
    }

    /**
     * True, if the system may send data
     * @Type {boolean}
     */
    set maySend (maySend) { this._maySend = maySend }
    get maySend () { return this._maySend }

    /**
     * Adress of the right sibling
     * @Type {number}
     */
    get rightSibling () { return this._rightSibling }
    set rightSibling (rightSibling) { this._rightSibling = rightSibling }

    /**
     * Adress of the leftmost child in the token chain
     * @Type {number}
     */
    get leftmostSibling () { return this._leftmostSibling }
    set leftmostSibling (leftmostSibling) { this._leftmostSibling = leftmostSibling }

    /**
     * True, if logging is witched on for debugging
     * @Type {number}
     */
    get trace () { return this._trace }
    set trace (trace) { this._trace = trace }

    /**
     * Gets the address of the next receiver in the node chain
     * @returns {number} address of the next receiver in the node chain
     */
    getReceiverAddress () {
        let result
        if (this.rightSibling !== null) {
            result = this.rightSibling
        } else if (this.leftmostSibling !== null) {
            result = this.leftmostSibling
        } else {
            result = BROADCAST_ADDRESS
        }
        return result
    }

    /**
     * Calculates an enable send request
     * It is either "enable send", if it is directly addressed to myself or a registration request, if it is boradcasted
     * @returns {number} enable send request
     * @private
     */
    calculateEnableSend () {
        var res = 0
        if (this.getReceiverAddress() === BROADCAST_ADDRESS) {
            res = REGISTRATION_REQUEST
        } else {
            res = ENABLE_SEND
        }
        return res
    }

    /**
     * Checks, if the current state is "Registered"
     * @returns {boolean} true, if the current state is "Registered"
     */
    isRegistered () {
        return this._state === STATE_REGISTERED
    }

    /**
     * returns a string representing the current state
     * @returns {string} current state
     */
    getStateString () {
        let result
        switch (this._state) {
        case STATE_UNKNOWN: result = 'Unknown'; break
        case STATE_REBOOT: result = 'Reboot'; break
        case STATE_SINGLE: result = 'Single'; break
        case STATE_UNREGISTERED: result = 'Unregistered'; break
        case STATE_REGISTERED: result = 'Registered'; break
        default: result = 'Undefined'
        }
        return result
    }

    /**
     * Updates the current state
     * @param {number} request state request value
     * @param {boolean} notForMe true, if the request is not addressed to myself
     * @returns {number} new state value
     */
    updateState (request, notForMe) {
        var res = STATE_UNCHANGED
        switch (this._state) {
        case STATE_UNKNOWN: res = this.processUnknown(request, notForMe); break
        case STATE_REBOOT: res = this.processReboot(request, notForMe); break
        case STATE_SINGLE: res = this.processSingle(request, notForMe); break
        case STATE_UNREGISTERED: res = this.processUnregistered(request, notForMe); break
        case STATE_REGISTERED: res = this.processRegistered(request, notForMe); break
        }
        return res
    }

    /**
     * Calculates a new state and updates when no message has been received
     * @returns {number} new state value
     */
    updateStateNoMessage () {
        let loopState
        let res = STATE_UNCHANGED
        if (this._timer >= MAX_WAIT_TIMER) {
            res = this.updateState(LOOP_TIMEOUT)
        } else {
            loopState = this._timer % TIMER_LOOP
            if (loopState === 0) {
                res = this.updateState(LOOP_START)
            } else if (loopState === TIMER_SMALL_PERIOD) {
                res = this.updateState(LOOP_SHORT_BREAK)
            } else if (loopState === TIMER_LARGE_PERIOD) {
                res = this.updateState(LOOP_LONG_BREAK)
            }
        }
        if (res !== STATE_CHANGED) {
            this._timer += 1
        }
        return res
    }

    /**
     * Sets a new state
     */
    setState (newState) {
        this._timer = 0
        this._state = newState
        if (this.trace) {
            console.log(
                'state changed to %s, leftmostSibling: %s, rightSibling: %s',
                this.getStateString(), this.leftmostSibling, this.rightSibling
            )
        }
    }

    /**
     * processes an enable send command (token) while in any other state than registered.
     * If the command is not for myself, we set the state to unregistered as we know now that there are other devices on the bus
     * If the command is for myself we set the state to registered, as we know that at least one device added us to this
     * device chain and will continue to send us tokens
     */
    processEnableSendWhenNotRegistered (notForMe) {
        if (notForMe) {
            this.setState(STATE_UNREGISTERED)
        } else {
            this.setState(STATE_REGISTERED)
            this.maySend = true
        }
        return STATE_CHANGED
    }

    /**
     * Processes a request while in "unknown" state
     * Unknown state happens after a reboot or after receiving data in a single state as the situation is unknown
     * The state reacts in waiting 100 ticks for external actions like a request to send registration information
     * Reason: we do not want to pollute the channel with unintended messages
     * @param {number} request request number
     * @param {boolean} notForMe true, if the request is not addressed to myself
     * @returns {number} state change information
     * @private
     */
    processUnknown (request, notForMe) {
        var res = STATE_UNCHANGED
        this.maySend = false
        switch (request) {
        case ENABLE_SEND:
            res = this.processEnableSendWhenNotRegistered(notForMe)
            break
        case REGISTRATION_INFO:
            // Only registration request will lead to a registration
            break
        case REGISTRATION_REQUEST:
            this.setState(STATE_UNREGISTERED)
            res = REGISTRATION_INFO
            break
        case LOOP_START:
            if (this._timer === 0) {
                this.rightSibling = null
                this.leftmostSibling = null
            }
            break
        case LOOP_TIMEOUT:
            this.setState(STATE_REBOOT)
            res = STATE_CHANGED
            break
        }
        return res
    }

    /**
     * Processes a request while in "reboot" state. In the reboot state everything is unknown.
     * Reboot state happens after a timeout of an unknown state, we exect all devices have been rebooted
     * The state reacts in sending a registration request every 10 ticks
     * Reason: motivate all other devices to send registration information to enable the registration process
     * @param {number} request request number
     * @param {boolean} notForMe true, if the request is not addressed to myself
     * @returns {number} state change information
     * @private
     */
    processReboot (request, notForMe) {
        var res = STATE_UNCHANGED
        this.maySend = false
        switch (request) {
        case ENABLE_SEND:
            res = this.processEnableSendWhenNotRegistered(notForMe)
            break
        case REGISTRATION_INFO:
            // Only a registration request will lead to a registration
            break
        case REGISTRATION_REQUEST:
            this.setState(STATE_UNREGISTERED)
            res = REGISTRATION_INFO
            break
        case LOOP_START:
            res = this.calculateEnableSend()
            break
        case LOOP_TIMEOUT:
            this.setState(STATE_SINGLE)
            res = STATE_CHANGED
            break
        }
        return res
    }

    /**
     * Processes a request while in "single" state.
     * This happens if no other devices is detected
     * The state reacts in sending registration requests every 10 ticks and will never timeout
     * Reason: there is nothing to do while beeing alone on the bus only checking if somebody joined
     * @param {number} request request number
     * @param {boolean} notForMe true, if the request is not addressed to myself
     * @returns {number} state change information
     * @private
     */
    processSingle (request, notForMe) {
        var res = STATE_UNCHANGED

        switch (request) {
        case ENABLE_SEND:
            res = this.processEnableSendWhenNotRegistered(notForMe)
            break
        case REGISTRATION_INFO:
            this.maySend = false
            this.setState(STATE_UNKNOWN)
            res = STATE_CHANGED
            break
        case REGISTRATION_REQUEST:
            this.maySend = false
            this.setState(STATE_UNREGISTERED)
            res = REGISTRATION_INFO
            break
        case LOOP_START:
            this.maySend = false
            res = REGISTRATION_REQUEST
            break
        case LOOP_SHORT_BREAK:
            this.maySend = true
            break
        case LOOP_TIMEOUT:
            this._timer = 0
            break
        }
        return res
    }

    /**
     * Processes a request while in "unregistered" state.
     * This happens if a registration request has been received
     * The state reacts in sending a registration info and waits for an enable send as registration acknowledge
     * Reason: registration process is invoked by another device and we cooperate
     * @param {number} request request number
     * @param {boolean} notForMe true, if the request is not addressed to myself
     * @returns {number} state change information
     * @private
     */
    processUnregistered (request, notForMe) {
        var res = STATE_UNCHANGED
        this.maySend = false
        switch (request) {
        case ENABLE_SEND:
            if (!notForMe) {
                this.setState(STATE_REGISTERED)
                this.maySend = true
            }
            res = STATE_CHANGED
            break
        case REGISTRATION_INFO:
            break
        case REGISTRATION_REQUEST:
            res = REGISTRATION_INFO
            break
        case LOOP_TIMEOUT:
            this.setState(STATE_UNKNOWN)
            res = STATE_CHANGED
            break
        }
        return res
    }

    /**
     * Sub function to process a registered state. Things we do, in the first part of a standard loop:
     * Checking for lost tokens, deactivating may send, requesting registrations of devices ...
     * @param {number} request request number
     * @param {boolean} notForMe true, if the request is not addressed to myself
     * @returns {number} state change information
     * @private
     */
    registeredShortLoopBreak () {
        var res = STATE_UNCHANGED
        this.tokenLost = (this._lastEnableSend + TIMEOUT_NO_ENABLE_SEND <= this._timer)
        if (this._timer === TIMER_SMALL_PERIOD || this.tokenLost) {
            this._lastEnableSend = this._timer
            this.maySend = false
            if (this.rightSibling === null && !this.tokenLost) {
                res = REGISTRATION_REQUEST
            } else {
                res = ENABLE_SEND
            }
        }
        return res
    }

    /**
     * Process a request while in registered state
     * This happens after receiving a registration request, answering with registration info and then getting an enable send
     * The state reacts in cooperating in the token chain. With enable send requests and registration requests if last element in the chain
     * Reason: only the device having the enable send token may send
     * @param {number} request request number
     * @param {boolean} notForMe true, if the request is not addressed to myself
     * @returns {number} state change information
     * @private
     */
    processRegistered (request, notForMe) {
        var res = STATE_UNCHANGED
        switch (request) {
        case ENABLE_SEND:
            if (!notForMe) {
                this.maySend = true
                this._timer = 0
            } else {
                this.maySend = false
                this._lastEnableSend = this._timer
            }
            break
        case REGISTRATION_INFO: break
        case REGISTRATION_REQUEST: break
        case LOOP_SHORT_BREAK:
            res = this.registeredShortLoopBreak()
            break
        case LOOP_LONG_BREAK:
            if (this._timer === TIMER_LARGE_PERIOD && this.rightSibling === null && this.leftmostSibling !== null) {
                res = ENABLE_SEND
            }
            break
        case LOOP_TIMEOUT:
            this.setState(STATE_UNREGISTERED)
            res = STATE_CHANGED
            break
        }
        return res
    }
}

module.exports = RS485State
