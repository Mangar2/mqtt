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

const SerialMessage = require('./serialmessage')
const RS485State = require('./rs485state')
const AddressChain = require('./addresschain')

const {
    ENABLE_SEND,
    REGISTRATION_INFO,
    REGISTRATION_REQUEST,
    BROADCAST_ADDRESS
} = require('./constants')

const TOKEN = '!'

/**
 * Creates a new token handler for RS485 based serial connections to arduinos
 * @param {Object} options configuration options
 * @param {integer} options.myAddress address of this device
 * @param {integer} options.maxVersion maximal suported interface version
 * @private
 */
class RS485TokenExchange {
    constructor (options) {
        this._myAddress = options.myAddress
        this._maxVersion = options.maxVersion
        this._version = this._maxVersion
        this._mayChangeVersion = false
        this._state = new RS485State()
        this._chain = new AddressChain()
    }

    /**
     * Binary lenght of the message if transported over a serial connection
     * @type {integer}
     */
    get version () { return this._version }
    set version (version) { this._version = version }

    /**
     * Enables the ability to change the interface version. This ability is activated
     * as soon as we allow any other attached device to send messages
     * @param {SerialMessage} messageSend last message send
     */
    enableChangeVersion (messageSend) {
        if (messageSend.value === ENABLE_SEND) {
            this._mayChangeVersion = true
        }
    }

    /**
     * Logs the current state to the console
     */
    logState () {
        console.log(this.getStateInfo())
    }

    /**
     * Gets a state info
     * @returns {string}
     */
    getStateInfo () {
        const result =
            'State: ' + this._state.getStateString() +
            ' Leftmost: ' + this._state.leftmostSibling +
            ' Neighbour: ' + this._state.rightSibling
        return result
    }

    /**
     * Signals sending right
     * @type {boolean}
     */
    get maySend () { return this._state.maySend }
    set maySend (maySend) { this._state.maySend = maySend }

    /**
     * Checks, if a message is addressed to myself
     * @param {SerialMessage} message serial message to check
     * @returns {boolean}
     * @private
     */
    _isForMe (message) {
        return message.receiver === BROADCAST_ADDRESS || message.receiver === this._myAddress
    }

    /**
     * Gets the address of the next receiver in the node chain
     * @returns {number} address of the next receiver
     * @private
     */
    _getReceiverAddress () {
        if (this._state.rightSibling === null) {
            if (this._state.leftmostSibling === null) {
                return BROADCAST_ADDRESS
            }
            return this._state.leftmostSibling
        } else {
            return this._state.rightSibling
        }
    }

    /**
     * Creates a message signaling a state
     * @param {number} stateUpdateResult value of the state to signal
     * @returns {SerialMessage|undefined} a state signaling message fitting to the result to the state update
     * @private
     */
    _createStateSignalingMessage (stateUpdateResult) {
        let message
        if (stateUpdateResult === REGISTRATION_INFO || stateUpdateResult === REGISTRATION_REQUEST || stateUpdateResult === ENABLE_SEND) {
            message = new SerialMessage(this._version)
            message.command = TOKEN
            message.value = stateUpdateResult
            message.sender = this._myAddress
            message.receiver = BROADCAST_ADDRESS
            message.reply = false
            if (stateUpdateResult === ENABLE_SEND) {
                message.receiver = this._getReceiverAddress()
            }
        }
        return message
    }

    /**
     * Updates the right sibling if needed
     * @param {integer} address address of the sender of the last message
     * @private
     */
    _updateRightSibling (address) {
        if (address > this._myAddress) {
            if (this._state.rightSibling === null || this._state.rightSibling > address) {
                this._state.rightSibling = address
            }
        }
    }

    /**
     * Updates the leftmost sibling if needed
     * @param {integer} address address of the sender of the last message
     * @private
     */
    _updateLeftmostSibling (address) {
        if (address < this._myAddress && address !== BROADCAST_ADDRESS) {
            if (this._state.leftmostSibling === null || this._state.leftmostSibling < address) {
                this._state.leftmostSibling = address
            }
        }
    }

    /**
     * Update the sender address chain, if the senderAddress is closer than current rightmost and leftmost ceibling
     * @param {integer} address address of the sender of the last message
     * @param {integer} version interface version of the sender
     * @private
     */
    _updateAddressChain (address, version) {
        this._updateRightSibling(address)
        this._updateLeftmostSibling(address)
        this._state.leftmostSibling = Math.min(this._state.leftmostSibling, address)
        this._chain.insert(address, version)
    }

    /**
     * Updates the interface version to use, if autoDetectVersion is set to true
     * @param {SerialMessage} message last token message received
     * @private
     */
    _updateVersion (message) {
        const version = message.version
        if (this._mayChangeVersion && message.value === ENABLE_SEND && version <= this._maxVersion) {
            this._version = version
        }
    }

    /**
     * Handles a new state message and adapts the status
     * @param {SerialMessage} message message received from an RS485 communication
     * @returns {SerialMessage|unknown} state signaling "reply" message for the token communcation
     */
    processStateMessage (receivedMessage) {
        let stateSignalingMessage
        if (receivedMessage.command === TOKEN) {
            const isForMe = this._isForMe(receivedMessage)
            this._updateAddressChain(receivedMessage.sender, receivedMessage.version)
            const stateValue = this._state.updateState(receivedMessage.value, !isForMe)
            stateSignalingMessage = this._createStateSignalingMessage(stateValue)
            this._updateVersion(receivedMessage)
        }
        return stateSignalingMessage
    }

    /**
     * Handler called each tick to check if any message must or may be sent
     * @returns {SerialMessage|unknown} state signaling "reply" message for the token communcation
     */
    processStateNoMessage () {
        const stateValue = this._state.updateStateNoMessage()
        const stateSignalingMessage = this._createStateSignalingMessage(stateValue)
        return stateSignalingMessage
    }
}

module.exports = RS485TokenExchange
