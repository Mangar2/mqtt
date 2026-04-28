/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Provides a service to invoke broker messages from external networks. Allows only configured messages.
 */

'use strict'

/**
 * Maps a topic to an associated pin configuration
 * @param {string} topic topic provided by the message
 * @param {Object} options service options
 * @returns {{ gpio:integer, invers:boolean }} gpio number and invers logic flag associated to this topic
 * @private
 */
function _topicToConfig(topic, options) {
    const switches = options.switches
    const gpioConfig = switches[topic]
    if (gpioConfig === undefined) {
        throw 'Unknown topic: ' + topic
    }
    return gpioConfig
}
 
/**
 * Calculates a value suitable to set/clear gpio from a message-value
 * @param {string|boolean|integer} value value of the switch
 * @param {boolean} config.invers true, if the switch logig is invers
 * @private
 */
function _calcGpioValue(value, config) {
    let resultValue = value === 'on' || value === true || value === 'true' || value === 1 || value === '1'
    if (config.invers) {
        resultValue = !resultValue
    }
    return resultValue === true ? 1 : 0
}
 
/**
 * Processes an incoming mqtt message
 * @param {Message} message mqtt message
 * @param {Object} options service options
 * @returns { gpio: number, value: [0,1] } gpio number and gpio value
 */
function messageToGpio (message, options) {
    const { topic, value } = message
    const config = _topicToConfig(topic, options)
    const { gpio } = config
    const gpioValue = _calcGpioValue(value, config)
    return { gpio, value: gpioValue }
}
     

module.exports = messageToGpio
 