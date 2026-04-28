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

const { types } = require('@mangar2/utils')
const { SWITCH_ON, SWITCH_OFF } = require('./constants')
const SerialMessage = require('./serialmessage')

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
class MQTTMessageToSerialMessage {
    constructor (options) {
        this._options = options
    }

    /**
     * Identifies the receiver address of the message
     * @param {string} interfaceName name of the interface
     * @param {string} topic mqtt topic
     * @returns {string} receiver address
     * @private
     */
    _getReceiverByTopic (interfaceName, topic) {
        let result = ''
        const receiverMap = this._options.interfaces[interfaceName].receiverMap
        if (types.isObject(receiverMap)) {
            for (const address in receiverMap) {
                if (topic.toLowerCase().startsWith(address.toLowerCase())) {
                    result = receiverMap[address]
                    break
                }
            }
        }
        return result
    }

    /**
     * Identifies the command of the message
     * @param {string} topic mqtt topic
     * @returns {{interfaceName: string, command: string}} command of the message
     * @throws {Error} if no command string matches the end of the topic
     * @private
     */
    _getInterfaceCommandByTopic (topic) {
        let result
        for (const interfaceName in this._options.interfaces) {
            const commandMap = this._options.interfaces[interfaceName].commandMap
            for (const command in commandMap) {
                if (topic.toLowerCase().endsWith(commandMap[command].toLowerCase())) {
                    result = { interfaceName, command }
                    break
                }
            }
            if (result !== undefined) {
                break
            }
        }
        if (result === undefined) {
            throw Error('undefined device setting ' + topic)
        }
        return result
    }

    /**
     * Gets command and value by topic
     * @param {Message} mqttMessage message received from mqtt Broker
     * @returns { SerialMessage } command, receiver and value to send
     * @private
     */
    _getSerialMessageByTopic (mqttMessage) {
        const { topic, value } = mqttMessage
        let result
        for (const interfaceName in this._options.interfaces) {
            const { topicMap } = this._options.interfaces[interfaceName]

            if (types.isObject(topicMap) && topicMap[topic] !== undefined) {
                const settings = topicMap[topic]
                result = new SerialMessage(interfaceName, null, settings.address, settings.command, settings.value)
                if (value === 'on' || value === '1') {
                    result.value += SWITCH_ON
                } else {
                    result.value += SWITCH_OFF
                }
            }
        }
        return result
    }

    /**
     * Maps a value name (like 'bright', 'dark', 'on', 'off') to a dedicated (numeric) value
     * @param {Object} valueMap value map of an interface containing the value name definitions
     * @param {string} command command name
     * @param {string} value value to map
     * @returns {string|number} returns the original string, if no mapping is found or a mapped value
     */
    _mapValue (valueMap, command, value) {
        let result = value
        if (types.isObject(valueMap)) {
            for (const itemName in valueMap) {
                const valueMapItem = valueMap[itemName]
                if (valueMapItem.usedby.includes(command)) {
                    const newValue = valueMapItem.map[value.toLowerCase()]
                    if (types.isInteger(newValue)) {
                        result = newValue
                        break
                    }
                }
            }
        } else {
            if (value === 'on' || value === 'true' || value === '1') {
                result = 1
            }
            if (value === 'off' || value === 'false' || value === '0') {
                result = 0
            }
        }
        return result
    }

    /**
     * Determines the value to set, depending of the command and the value. It looks up the
     * interface defintions to map strings (like 'on' or 'off') to dedicated values and just returns
     * the mqttValue on any number
     * @param {SerialMessage} serialMessage partially created serial message, value not jet set
     * @param {string} serialMessage.interfaceName name of the interface to use
     * @param {string} serialMessage.command command to use in the interface
     * @param {number|string} mqttValue value of the mqtt message
     * @returns {number} value to be send as serial message
     * @throws {Error} if the value is not in the range between 0 and 0xFFFF
     * @private
     */
    _getSerialValueByCommand (serialMessage, mqttValue) {
        const { interfaceName, command } = serialMessage
        const microControllerInterface = this._options.interfaces[interfaceName]
        const valueMap = microControllerInterface.valueMap
        let result = mqttValue
        if (!isNaN(result)) {
            result = Number(result)
        }
        if (types.isString(result)) {
            result = this._mapValue(valueMap, command, mqttValue)
            if (result === 'on') { result = 1 }
            if (result === 'off') { result = 0 }
        }
        if (!types.isInteger(result)) {
            throw Error('The provided value is not an integer: ' + mqttValue)
        } else if (mqttValue < 0 || mqttValue > 0xFFFF) {
            throw Error('The provided value is not a positive two byte value; 0 <= value <= 0xFFFF: ' + mqttValue)
        }

        return result
    }

    /**
     * Converts a mqtt message to a serial message
     * @param {Message} mqttMessage message received from mqtt Broker
     * @returns {Object} message to send
     * @throws {Error} if any error occured and the message would not be valid
     */
    toSerialMessage (mqttMessage) {
        let serialMessage = this._getSerialMessageByTopic(mqttMessage)
        if (serialMessage === undefined) {
            serialMessage = new SerialMessage()
            serialMessage.sender = null
            const { interfaceName, command } = this._getInterfaceCommandByTopic(mqttMessage.topic)
            serialMessage.command = command
            serialMessage.interfaceName = interfaceName
            serialMessage.receiver = this._getReceiverByTopic(interfaceName, mqttMessage.topic)
            serialMessage.value = this._getSerialValueByCommand(serialMessage, mqttMessage.value)
        }
        return serialMessage
    }
}

module.exports = MQTTMessageToSerialMessage
