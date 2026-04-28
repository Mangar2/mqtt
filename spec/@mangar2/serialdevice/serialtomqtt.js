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

const Message = require('@mangar2/message')
const { types } = require('@mangar2/utils')
const { SWITCH_ON, SWITCH_OFF } = require('./constants')

/**
 * "Domain name service" with the ability to convert serial messages in mqtt messages and vice versa
 * @param {Object} options matching options
 * @param {Object} options.settings setting commands
 * @param {Object} options.status status commands
 * @param {Object} options.topics full topic command/value settings
 * @param {Object} options.addresses location addresses
 * @param {Object} options.interfaces interface defintions
 * @param {integer} options.myAddress sender address
 * @private
 */
class SerialToMQTT {
    constructor (options) {
        this._options = options
    }

    /**
     * Determines the value to set, depending of the command and the value. It looks up the
     * interface defintions to map strings (like 'on' or 'off') to dedicated values and just returns
     * the mqttValue on any number
     * @param {Object} interfaceDefinition interface definition
     * @param {Object} interfaceDefinition.valueMap key/value map to map values to value strings
     * @param {char} command one character string containing the command
     * @returns {number|string} value to be send in a mqtt message
     * @private
     */
    _toMqttValue (interfaceDefinition, command, serialValue) {
        let result = serialValue
        const valueMap = interfaceDefinition.valueMap
        if (types.isObject(valueMap)) {
            for (const itemName in valueMap) {
                const valueMapItem = valueMap[itemName]
                if (valueMapItem.usedby.includes(command)) {
                    for (const interfaceValue in valueMapItem.map) {
                        if (valueMapItem.map[interfaceValue] === serialValue) {
                            result = interfaceValue
                            break
                        }
                    }
                }
            }
        }

        return result
    }

    /**
     * Gets the begin of the topic derived from the serial address
     * @param {Object} interfaceDefinition interface definition
     * @param {Object} interfaceDefinition.receiverMap key/value map to map addresses to topics
     * @param {integer} serialAddress address of a serial device
     * @returns {string} begin of the topic
     * @throws {Error} if the serial address is unknown
     * @private
     */
    _getTopicBeginUsingSerialAddress (interfaceDefinition, serialAddress) {
        let result = ''

        const receiverMap = interfaceDefinition.receiverMap
        for (const topicBegin in receiverMap) {
            const address = receiverMap[topicBegin]
            if (serialAddress === address || serialAddress === address.toString()) {
                result = topicBegin
            }
        }
        if (result === '' && serialAddress !== null) {
            throw Error('Unknown serial address: ' + serialAddress)
        }
        return result
    }

    /**
     * Gets the end of the topic derived from the serial command
     * @param {Object} interfaceDefinition interface definition
     * @param {Object} interfaceDefinition.commandMap key/value map to map commands to topics
     * @param {string} serialCommand command of a serial message
     * @returns {string} end of the topic
     * @throws {Error} if the serial command is unknown
     * @private
     */
    _getTopicEndUsingSerialCommand (interfaceDefinition, serialCommand) {
        let result
        const commandMap = interfaceDefinition.commandMap
        if (types.isObject(commandMap)) {
            result = commandMap[serialCommand]
        }
        const sendMap = interfaceDefinition.sendMap
        if (!result && types.isObject(sendMap)) {
            result = sendMap[serialCommand]
        }
        if (result === undefined) {
            throw Error('Unknown serial command: ' + serialCommand)
        }
        return result
    }

    /**
     * Gets the topic derived from a serial message
     * @param {Object} serialMessage received serial message
     * @returns {string} topic
     * @throws {Error} If the topic cannot be derived due to wrong or not configured elements
     * @private
     */
    _getTopicUsingSerialMessage (serialMessage) {
        const { interfaceName, sender, command } = serialMessage
        const interfaceDefinition = this._options.interfaces[interfaceName]
        if (interfaceDefinition === undefined) {
            throw Error('Unknown interface name: ' + interfaceName)
        }
        const topic =
            this._getTopicBeginUsingSerialAddress(interfaceDefinition, sender) +
            this._getTopicEndUsingSerialCommand(interfaceDefinition, command) +
            serialMessage.action
        return topic
    }

    /**
     * Check, if we can get mqtt messages from the "topics" interface of options
     * @param {Object} interfaceDefinition interface definition
     * @param {Object} interfaceDefinition.topicMap key/value map to map switching values to topics
     * @param {SerialMessage} serialMessage received serial message
     * @returns {Message[]} list of mqtt messages to send to broker
     * @private
     */
    _getMqttMessageOnSwitchMessages (interfaceDefinition, serialMessage) {
        const result = []
        const { command, sender, value } = serialMessage
        const topicMap = interfaceDefinition.topicMap
        for (const topic in topicMap) {
            const requiredSerialData = topicMap[topic]
            if (command === requiredSerialData.command && sender === requiredSerialData.address) {
                const isSwitchOnMessage = (value & SWITCH_ON) !== 0
                const isSwitchOffMessage = (value & SWITCH_OFF) !== 0
                const isSwitchMessage = isSwitchOnMessage || isSwitchOffMessage
                const bitIsSet = (value & requiredSerialData.value) !== 0
                if (!isSwitchMessage) {
                    const mqttValue = bitIsSet ? 'on' : 'off'
                    result.push(new Message(topic, mqttValue, 'received from arduino'))
                } else if (bitIsSet) {
                    const mqttValue = isSwitchOffMessage ? 'off' : 'on'
                    result.push(new Message(topic, mqttValue, 'received from arduino'))
                }
            }
        }
        return result
    }

    /**
     * Converts a serial message to mqtt messages
     * @param {SerialMessage} serialMessage received serial object
     * @returns {Message[]} Mqtt messages
     * @throws {Error} If the message cannot be derived due to wrong or not configured elements
     */
    toMqttMessages (serialMessage) {
        let result = []
        const { interfaceName, command, value } = serialMessage
        const interfaceDefinition = this._options.interfaces[interfaceName]
        if (interfaceName === 'switch') {
            result = this._getMqttMessageOnSwitchMessages(interfaceDefinition, serialMessage)
        } else {
            const message = new Message(
                this._getTopicUsingSerialMessage(serialMessage),
                this._toMqttValue(interfaceDefinition, command, value),
                'received from arduino'
            )
            result.push(message)
        }
        return result
    }
}

module.exports = SerialToMQTT
