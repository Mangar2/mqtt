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
const Message = require('@mangar2/message')
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
class SerialDNS {
    constructor (options) {
        this._options = options
    }

    /**
     * Identifies the receiver address of the message
     * @param {string} topic mqtt topic
     * @param {SerialMessage} serialMessage serial message to update
     * @returns {integer} receiver address
     * @throws {Error} if no address string matches the begin of the topic
     * @private
     */
    _getReceiverByTopic (topic) {
        let result
        for (const address in this._options.addresses) {
            if (topic.toLowerCase().startsWith(address.toLowerCase())) {
                result = this._options.addresses[address]
                break
            }
        }
        if (result === undefined) {
            throw Error('undefined device address ' + topic)
        }
        return result
    }

    /**
     * Identifies the command of the message
     * @param {string} topic mqtt topic
     * @returns {string} command of the message
     * @throws {Error} if no command string matches the end of the topic
     * @private
     */
    _getCommandByTopic (topic) {
        let result
        for (const setting in this._options.settings) {
            if (topic.toLowerCase().endsWith(this._options.settings[setting].toLowerCase())) {
                result = setting
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
     * @returns {{command: string, address: number, value: number}} command, receiver and value to send
     * @private
     */
    _getSerialDataByTopic (mqttMessage) {
        const { topic, value } = mqttMessage
        const { topics } = this._options
        let setting
        if (topics !== undefined && topics[topic] !== undefined) {
            setting = { ...this._options.topics[topic] }
            if (value === 'on' || value === '1') {
                setting.value += SWITCH_ON
            } else {
                setting.value += SWITCH_OFF
            }
        }
        return setting
    }

    /**
     * Determines the value to set, depending of the command and the value. It looks up the
     * interface defintions to map strings (like 'on' or 'off') to dedicated values and just returns
     * the mqttValue on any number
     * @param {char} command one character string containing the command
     * @param {number|string} mqttValue value of the mqtt message
     * @returns {number} value to be send as serial message
     * @throws {Error} if the value is not in the range between 0 and 0xFFFF
     * @private
     */
    _toSerialValue (command, mqttValue) {
        let result = mqttValue
        if (!isNaN(result)) {
            result = Number(result)
        }
        if (types.isString(result)) {
            for (const interfaceName in this._options.interfaces) {
                const arduinoInterface = this._options.interfaces[interfaceName]
                if (arduinoInterface.usedby.includes(command)) {
                    const newValue = arduinoInterface.map[result.toLowerCase()]
                    if (types.isInteger(newValue)) {
                        result = newValue
                    }
                }
            }
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
     * @returns {SerialMessage} message to send
     * @throws {Error} if any error occured and the message would not be valid
     */
    toSerialMessage (mqttMessage) {
        const serialMessage = new SerialMessage()
        serialMessage.sender = this._options.myAddress
        if (this._options.myAddress < 1 || this._options.myAddress > 127) {
            throw Error('Configuration problem, sender address must be between 1 and 127')
        }
        const data = this._getSerialDataByTopic(mqttMessage)
        if (data) {
            serialMessage.receiver = data.address
            serialMessage.command = data.command
            serialMessage.value = data.value
        } else {
            serialMessage.receiver = this._getReceiverByTopic(mqttMessage.topic)
            serialMessage.command = this._getCommandByTopic(mqttMessage.topic)
            serialMessage.value = this._toSerialValue(serialMessage.command, mqttMessage.value)
        }
        return serialMessage
    }

    /**
     * Determines the value to set, depending of the command and the value. It looks up the
     * interface defintions to map strings (like 'on' or 'off') to dedicated values and just returns
     * the mqttValue on any number
     * @param {char} command one character string containing the command
     * @returns {number|string} value to be send in a mqtt message
     * @private
     */
    _toMqttValue (command, serialValue) {
        let result = serialValue

        for (const interfaceName in this._options.interfaces) {
            const arduinoInterface = this._options.interfaces[interfaceName]
            if (arduinoInterface.usedby.includes(command)) {
                for (const interfaceValue in arduinoInterface.map) {
                    if (arduinoInterface.map[interfaceValue] === serialValue) {
                        result = interfaceValue
                        break
                    }
                }
            }
        }

        return result
    }

    /**
     * Gets the begin of the topic derived from the serial address
     * @param {integer} serialAddress address of a serial device
     * @returns {string} begin of the topic
     * @throws {Error} if the serial address is unknown
     * @private
     */
    _getTopicBeginUsingSerialAddress (serialAddress) {
        let result
        for (const topicBegin in this._options.addresses) {
            const address = this._options.addresses[topicBegin]
            if (serialAddress === address) {
                result = topicBegin
            }
        }
        if (result === undefined) {
            throw Error('Unknown serial address: ' + serialAddress)
        }
        return result
    }

    /**
     * Gets the end of the topic derived from the serial command
     * @param {string} serialCommand command of a serial message
     * @returns {string} end of the topic
     * @throws {Error} if the serial command is unknown
     * @private
     */
    _getTopicEndUsingSerialCommand (serialCommand) {
        let result
        const settings = this._options.settings[serialCommand]
        if (settings) {
            result = settings
        } else {
            const status = this._options.status[serialCommand]
            if (status) {
                result = status
            }
        }
        if (result === undefined) {
            throw Error('Unknown serial command: ' + serialCommand)
        }
        return result
    }

    /**
     * Gets the topic derived from a serial message
     * @param {SerialMessage} serialMessage received serial message
     * @returns {string} topic
     * @throws {Error} If the topic cannot be derived due to wrong or not configured elements
     * @private
     */
    _getTopicUsingSerialMessage (serialMessage) {
        const topic =
            this._getTopicBeginUsingSerialAddress(serialMessage.sender) +
            this._getTopicEndUsingSerialCommand(serialMessage.command)
        return topic
    }

    /**
     * Check, if we can get mqtt messages from the "topics" interface of options
     * @param {SerialMessage} serialMessage serial message
     * @param {string} hexString hexadecimal string of the message
     * @returns {Message[]} list of mqtt messages to send to broker
     * @private
     */
    _getMqttMessageUsingSerialMessage (serialMessage, hexString) {
        const result = []
        const { command, sender, value } = serialMessage
        for (const topic in this._options.topics) {
            const requiredSerialData = this._options.topics[topic]
            if (command === requiredSerialData.command && sender === requiredSerialData.address) {
                const isSwitchOnMessage = (value & SWITCH_ON) !== 0
                const isSwitchOffMessage = (value & SWITCH_OFF) !== 0
                const isSwitchMessage = isSwitchOnMessage || isSwitchOffMessage
                const bitIsSet = (value & requiredSerialData.value) !== 0
                if (!isSwitchMessage) {
                    const mqttValue = bitIsSet ? 'on' : 'off'
                    result.push(new Message(topic, mqttValue, 'received from arduino: ' + hexString))
                } else if (bitIsSet) {
                    const mqttValue = isSwitchOffMessage ? 'off' : 'on'
                    result.push(new Message(topic, mqttValue, 'received from arduino: ' + hexString))
                }
            }
        }
        return result
    }

    /**
     * Converts a serial message to mqtt messages
     * @param {SerialMessage} serialMessage received serial message
     * @returns {Message[]} Mqtt messages
     * @throws {Error} If the message cannot be derived due to wrong or not configured elements
     */
    toMqttMessages (serialMessage) {
        const result = this._getMqttMessageUsingSerialMessage(serialMessage, '')
        if (result.length === 0) {
            const { command, value } = serialMessage
            const message = new Message(
                this._getTopicUsingSerialMessage(serialMessage),
                this._toMqttValue(command, value),
                'received from arduino'
            )
            result.push(message)
        }
        return result
    }
}

module.exports = SerialDNS
