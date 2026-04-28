/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

const MessageStore = require('./messagestore')

/**
 * Prepares the object
 * @param {Object} config automation configuration
 * @param {MessageStore} [messagestore=null] existing automation class
 * @returns {MessageStore} the prepared automation object
 */
function prepare (config, messagestore = null) {
    if (!messagestore) {
        messagestore = new MessageStore(config)
    }

    return messagestore
}

module.exports = { prepare, MessageStore }
