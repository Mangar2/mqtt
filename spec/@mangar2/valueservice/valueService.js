/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * Values bases on a list of key/value pairs where the keys are strings in "topic" format, e.g. "a/b/c/d" 
 * This service provides the ability to read, store and deliver values to a mqtt-based broker
 */
'use strict'
const DEBUG = false
 
const sanitize = require('@mangar2/valueservice/configuration.js')
const Message = require('@mangar2/message')
const Callbacks = require('@mangar2/callbacks')
const CheckInput = require('@mangar2/checkinput')
 
const errorLog = (err) => { require('@mangar2/errorlog')(err, DEBUG) }

/**
 * JSON schema to check configuration input
 */
const checkVariableFile = new CheckInput({
    title: 'Variable configuration',
    type: 'object',
    additionalProperties: {
        type: ['string', 'integer']
    }
})
 
/**
  * Creates a values  class. Values holds variables with values
  * This class is designed to work togehter with the runservices service 
  * @param {Object} configuration
  * @param {number} [configuration.subscribeQoS=1] quality of service for message subscription
  */
class ValueService {
    constructor (configuration) {
        const sanitizedOptions = sanitize(configuration)
        this._callbacks = new Callbacks(['publish'])
        this._presenceTopic = sanitizedOptions.presenceTopic
        this._subscribeQoS = sanitizedOptions.subscribeQoS
        this._variables = {}
    }

    /**
     * Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback
     * @param {function} callback function(...parameter)
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) {
        this._callbacks.on(event, callback)
    }
    
    /**
         * Publishes a command based on a service definition
         * @param {string} deviceId identifier of the device
         * @param {string|number} value value to publish
         * @param {Service} service service definition
         * @private
         */
    _publish (message) {
        this._callbacks.invokeCallback('publish', message)
    }
 
    /**
      * Sets/replaces the rules tree
      * @param {Object} rulesTree tree with 'rules' elements
      */
    setVariables (variables) {
        if (checkVariableFile.validate(variables)) {
            this._variables = { ...variables } 
        } else {
            errorLog('variable file contains illegal formats \n\r' + 
                JSON.stringify(checkVariableFile.messages, null, 2))
        }
    }
 
    /**
      * Sets a single variable
      * @param {string} name name of the variable
      * @param {string|number} value value of the variable
      * @private
      */
    _setVariable (name, value) { 
        this._variables[name] = value
    }

    /**
      * Checks receives messages and sets variables for any "set" message
      * @param {Message} message received message
      * @returns {Message[]} list of messages as result 
      */
    processMessage (message) {
        const { topic, value, reason } = message
        const resultMessages = []
        
        if (topic.endsWith('/set')) {
            const variableName = topic.replace('/set', '')
            this._setVariable(variableName, value)
            const replyMessage = new Message(variableName, value, reason) 
            replyMessage.addReason('Received by broker')
            replyMessage.qos = this._subscribeQoS
            replyMessage.retain = true
            resultMessages.push(replyMessage)
        }
        return resultMessages
    }
 
    /**
      * Get all the variable set commands to subscribe to
      * @returns {string[]} array of subscriptions
      */
    getSubscriptions () {
        const subscriptions = {}
        for (const key in this._variables) {
            if (Object.hasOwnProperty.call(this._variables, key)) {
                const keyWithSet = key + '/set'
                subscriptions[keyWithSet] = this._subscribeQoS
            }
        }
        return subscriptions
    }
 
    /**
      * Fully handles an mqtt message
      * @param {Message} mqttMessage the received mqtt message
      * @returns {Message[]} array of reply messages
      */
    handleMessage (mqttMessage) {
        const responseMessages = this.processMessage(mqttMessage)
        return responseMessages
    }

    /**
     * Called once after registration to handle additional tasks
     */
    run () {
        for (const variableName in this._variables) {
            if (Object.hasOwnProperty.call(this._variables, variableName)) {
                const value = this._variables[variableName]
                const message = new Message(variableName, value, 'value service restart') 
                message.qos = this._subscribeQoS
                message.retain = true
                this._publish(message)
            }
        }
    }
}
 
module.exports = ValueService
 