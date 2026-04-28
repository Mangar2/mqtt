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

const { types, Retry, Callbacks } = require('@mangar2/utils')
const Message = require('@mangar2/message')
const { exec } = require('child_process')

/**
 * Command configuration structure
 * @typedef {Object} Command
 * @property {string} name name of the command (must be unique across all commands)
 * @property {string} command command to execute
 * @property {number} delayInSeconds delay in seconds to wait before executing the command
 * @property {string} value state value sended as a return message when executing the command
 */

/**
 * Runs a configured operating-system command based on a message received
 * @param {Object} options configuration options
 * @param {Command|Command[]} options.commands command configuration
 * @param {string} options.clientId id of the client running the command
 */
class RunCommand {
    constructor (options) {
        const DELAY_FIRST_CALL = false
        this._options = options
        this._retry = new Retry(DELAY_FIRST_CALL)
        this._callbacks = new Callbacks(['publish'])
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
     * Gets a command from options
     * @param {string} name command name
     * @returns {Command} command object
     * @private
     */
    _getCommand (name) {
        let result = null
        if (types.isObject(this._options)) {
            let commands = this._options.commands
            if (!types.isArray(commands)) {
                commands = [commands]
            }
            for (const command of commands) {
                if (command.name === name) {
                    result = command
                }
            }
        }
        return result
    }

    /**
     * Executes a command, asyncronous helper
     * @param {Message} message message received
     * @param {string} message.value name of the command to execute
     * @returns {Promise} return message
     * @private
     */
    async _execute (message) {
        const { value } = message
        const command = this._getCommand(value).command
        const result = new Promise((resolve, reject) => {
            exec(command, (err, stdout, stderr) => {
                if (err) {
                    const returnMessage = new Message('$SYS/' + this._options.clientId, 'error', message.reason)
                    returnMessage.addReason('message: ' + err)
                    reject(returnMessage)
                } else {
                    const returnMessage = new Message('$SYS/' + this._options.clientId, 'success', message.reason)
                    returnMessage.addReason(`stdout: ${stdout}; stderr: ${stderr}`)
                    resolve(returnMessage)
                }
            })
        })
        return result
    }

    /**
     * Excecutes a command coming from a message. The command is taken from the value
     * @param {Message} message message received
     * @param {string} message.value command name. If it is 'stop', the last command will be cancelled
     * @param {string} message.topic topic of the command. A new message will terminate the (delayed)
     * command of the last call with the same topic
     */
    async processMessage (message) {
        const { topic, value } = message
        if (value === 'stop') {
            this._retry.stopRetry(topic)
        } else {
            const command = this._getCommand(value)
            const delayInSeconds = types.isNumber(command.delayInSeconds) ? command.delayInSeconds : 0
            const hibernateMessage = new Message(this._options.subscribeTopic, command.value, message.reason)
            hibernateMessage.addReason('command executed')
            this._callbacks.invokeCallback('publish', hibernateMessage)

            this._retry.topicRetry(topic, 1, delayInSeconds * 1000, async () => {
                const returnMessage = await this._execute(message)
                this._callbacks.invokeCallback('publish', returnMessage)
            })
        }
    }
}

module.exports = RunCommand
