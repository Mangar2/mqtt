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
 * Derives subscriptions from a topic list
 * @param {{topic:string}} topicMap object with topic properties
 * @param {0,1,2} qos quality of service for the subscription
 * @returns {{topic: qos}} object with topic: qos entries
 */
const _deriveSubscribesFromTopicMap = (topicMap, qos) => {
    const subscribes = {}
    for (const topic in topicMap) {
        subscribes[topic + '/+'] = qos
    }
    return subscribes
}

/**
 * Creates a list of subscribes starting with topicBegin and ending with all strings of the commandMap
 * @param {Object} commandMap map commands to topic end strings
 * @param {string} topicBegin begin of the topic
 * @param {number} qos quality of service
 */
const _deriveSubscribesFromCommandMap = (commandMap, topicBegin, qos) => {
    const subscribes = {}
    for (const command in commandMap) {
        const topicEnd = commandMap[command]
        subscribes[topicBegin + topicEnd + '/+'] = qos
    }
    return subscribes
}

const _deriveSubscribesFromCommandAndReceiverMap = (commandMap, receiverMap, qos) => {
    let subscribes = {}
    for (const topicBegin in receiverMap) {
        subscribes = {
            ...subscribes,
            ..._deriveSubscribesFromCommandMap(commandMap, topicBegin, qos)
        }
    }
    if (!receiverMap) {
        subscribes = _deriveSubscribesFromCommandMap(commandMap, '', qos)
    }
    return subscribes
}

/**
 * Derive the subscribe settings from the configuration options
 * @param {Object} options configuration options
 * @param {Array} options.interfaces array of interfaces
 * @param {0|1|2} options.qos quality of service for communication
 * @private
 */
const deriveSubscribes = (options) => {
    let subscribes = {}
    const { qos, interfaces } = options

    for (const interfaceName in interfaces) {
        const { topicMap, commandMap, receiverMap } = options.interfaces[interfaceName]
        subscribes = {
            ...subscribes,
            ..._deriveSubscribesFromTopicMap(topicMap, qos),
            ..._deriveSubscribesFromCommandAndReceiverMap(commandMap, receiverMap, qos)
        }
    }
    subscribes['$SYS/serialdevice/#'] = options.qos
    return subscribes
}

module.exports = { deriveSubscribes }
