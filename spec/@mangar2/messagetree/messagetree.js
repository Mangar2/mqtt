/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * This class manages a message store in tree format. yaha home automation uses it to store all
 * communication between its components.
 */
'use strict'

const Persist = require('@mangar2/persist')
const addToHistory = require('./addtohistory')
const decompress = require('./decompress')
const cleanup = require('./cleanup')

var persist = new Persist()

/**
 * @private
 * @description Checks, if a function is a string
 * @param {any} x
 */
function isString (x) {
    return Object.prototype.toString.call(x) === '[object String]'
}

/**
 * @private
 * @description Savely gets a property from an object. Sets the property, if it is not defined
 * @param {Object} theObject to get entry from
 * @param {string} propertyName name of the property to retrieve
 * @param {any} propertyValue value to set to the property, if the property is undefined
 * @returns property of the object or undefined, if the property is not retrievable
 */
function getFromObject (theObject, propertyName, propertyValue) {
    var result
    if (theObject !== undefined && propertyName !== undefined && isString(propertyName)) {
        if (!Object.hasOwnProperty.call(theObject, propertyName) && propertyValue !== undefined) {
            theObject[propertyName] = propertyValue
        }
        result = theObject[propertyName]
    }
    return result
}

/**
 * @typedef {Object} MessageNode
 * @property {number} time timestamp of last entry in the node
 * @property {number} value node value
 * @property {Reason} reason node reason
 * @property {History} history node history
 * @property {Map} childs map of child names to child.
 */

/**
 * @description Data structure holding a large amount of messages with topic, value and reason in a tree organized according the topic.
 * Each tree node containing data provides a "history" including recent messages.
 * It includes several methods to "compress" the history of messages of each topic
 * @param {Options} [options={}] options for the tree
 */

class MessageTree {
    constructor (options = {}) {
        this.options = {}
        this._setFromConfiguration('maxHistoryLength', options, 50)
        this._setFromConfiguration('historyHysterese', options, 10)
        this._setFromConfiguration('maxValuesPerHistoryEntry', options, 256)
        this._setFromConfiguration('lengthForFurtherCompression', options, 10)
        this._setFromConfiguration('upperBoundFactor', options, 1.2)
        this._setFromConfiguration('upperBoundAddInMilliseconds', options, 1000)
        this._setFromConfiguration('lowerBoundFactor', options, 0.8)
        this._setFromConfiguration('lowerBoundSubInMilliseconds', options, 1000)
        if (this.options.lengthForFurtherCompression === 1 || this.options.lengthForFurtherCompression === 2) {
            this.options.lengthForFurtherCompression = 3
        }
        this._tree = { childs: {} }
    }

    /**
     * @private
     * @description Safely sets a options option, if the option is included in options and it is a number
     * Strings with numbers are converted to integer
     * @param {string} propertyName name of the property to set
     * @param {Object} options Object containing the property
     * @param {number} defaultValue default value of the property
     */
    _setFromConfiguration (propertyName, options, defaultValue) {
        this.options[propertyName] = isNaN(options[propertyName]) ? defaultValue : Number(options[propertyName])
    }

    /**
     * Persists all connections
     * @param {string} directory directory of the files
     * @param {string} filenameBasis basis filename, a timestamp is added
     */
    async persist (directory, filenameBasis) {
        await persist.saveObjectToFile(directory, filenameBasis, this._tree)
    }

    /**
     * @description Reads the tree from a persisted file
     * @param {string} directory directory of the files
     * @param {string} filenameBasis basis filename, a timestamp is added
     */
    readTreeFromPersistedFile (directory, filenameBasis) {
        const newTree = persist.readData(directory, filenameBasis)
        if (newTree !== undefined) {
            this._tree = newTree
            console.log('sucessfully loaded persisted data')
        }
    }

    /**
     * @private
     * @description Checks if a node is a leaf node
     * @param {Object} node node of the message tree
     * @returns {boolean} true, if the node is a node with data
     */
    _isDataNode (node) {
        return Object.hasOwnProperty.call(node, 'history')
    }

    /**
     * @private
     * @description Initializes a leaf node
     * @param {Object} node
     */
    _initDataNode (node) {
        if (!this._isDataNode(node)) {
            node.history = []
        }
    }

    /**
     * @private
     * @description Gets the payload (time, data and history) of the node
     * @param {Object} node tree node
     * @param {boolean} addHistory true, if the history property will be added
     * @param {boolean} addReason true, if the reason property of the messages will be added
     * @returns {Object} payload of the node
     */
    _getPayload (node, addHistory, addReason) {
        const result = {
            topic: node.topic,
            value: node.value
        }
        if (addReason) {
            result.time = new Date(node.time).toISOString()
            result.reason = node.reason
        }
        if (addHistory) {
            result.history = decompress(node.history)
        }
        // restores additional (non usual properties)
        const exclude = ['history', 'childs', 'reason', 'time']
        for (const property in node) {
            if (result[property] === undefined && !exclude.includes(property)) {
                result[property] = node[property]
            }
        }
        return result
    }

    /**
     * @description Searches a node in the message tree corresponding to the topic.
     * @param {string} topic topic to store the data
     * @returns {Object} data to store
     */
    getNode (topic) {
        const topicChunks = topic.split('/')
        if (topicChunks[0] === '') {
            topicChunks.shift()
        }
        let childNode = this._tree
        for (const topicChunk of topicChunks) {
            childNode = getFromObject(childNode.childs, topicChunk)
            if (childNode === undefined) {
                break
            }
        }
        return childNode
    }

    /**
     * @description Searches a node in the message tree corresponding to the topic. Creates empty nodes along the topic if
     * the nodes are not yet available.
     * @param {string} topic topic to store the data
     * @returns {Object} node in the tree
     * @private
     */
    _getAndCreateDataNode (topic) {
        const topicChunks = topic.split('/')
        if (topicChunks[0] === '') {
            topicChunks.shift()
        }
        let node = this._tree
        for (const topicChunk of topicChunks) {
            node = getFromObject(node.childs, topicChunk, { childs: {} })
        }
        this._initDataNode(node)
        return node
    }

    /**
     * @private
     * @description
     * Updates the data of a node and stores the old data to a history array.
     * The new history entry is added to the beginning of the queue. If the history length exceeds the
     * maximal length, it will be reduces by "historyHysterese" amount of elements
     * @param {Object} node node to update
     * @param {Object} content new content of the node
     * @param {Object} options options to organize the history
     */
    _updateContent (node, content, options) {
        if (Object.hasOwnProperty.call(node, 'value')) {
            const historyEntry = { time: node.time, value: node.value, reason: node.reason }
            node.history = addToHistory(node.history, historyEntry, options)
        }
        for (const property in content) {
            node[property] = content[property]
        }
    }

    /**
     * @description Adds data to the three node matching to the topic path.
     * It will store the current time, data and descrition to the node and
     * the former time and data entry is copied to the history array property.
     * @param {Message} message {topic, value, reason}
     * @param {string} message.topic topic string
     * @param {objecObjectt} message.value value of the topic
     * @param {Object[]} message.reason list of reasons
     * @param {Date} [cur=new Date()] current time
     */
    addData (message, cur = new Date()) {
        const node = this._getAndCreateDataNode(message.topic)
        const time = cur.getTime()
        const content = { time, topic: message.topic, value: message.value, reason: message.reason }

        this._updateContent(node, content, this.options)
    }

    /**
     * @private
     * @description Gets several levels of child nodes in one flat object structure
     * @param {Object} node node of the message tree
     * @param {number} levelAmount amount of child levels to include
     * @param {boolean} addHistory true, if the history property will be added
     * @param {boolean} addReason true, if the reason property of the messages will be added
     * @returns {{payload: Object[]}} section of the tree as array of nodes
     */
    _getSectionRec (node, levelAmount, addHistory, addReason) {
        let result = { payload: [] }

        if (node !== undefined) {
            if (this._isDataNode(node)) {
                result.payload = [this._getPayload(node, addHistory, addReason)]
            }

            if (levelAmount > 0) {
                for (const childName in node.childs) {
                    const childNode = node.childs[childName]
                    const subChildNodes = this._getSectionRec(
                        childNode, levelAmount - 1, addHistory, addReason)
                    result = {
                        payload: [...result.payload, ...subChildNodes.payload]
                    }
                }
            }
        }
        return result
    }

    /**
     * Compares two reason objects
     * @param {Object|undefined} reason1 first reason to compare
     * @param {Object|undefined} reason2 second reason to compare
     * @returns {boolan} true, if reasons are identical or ANY reason is undefined, else false
     */
    _isRequiredReason (requiredeReason, existingReason) {
        if (requiredeReason === undefined || existingReason === undefined) {
            return true
        }
        if (requiredeReason.length !== existingReason.length) {
            return false
        }
        for (const index in requiredeReason) {
            if (requiredeReason[index].message !== existingReason[index].message ||
                requiredeReason[index].timestamp !== existingReason[index].timestamp) {
                return false
            }
        }
        return true
    }

    /**
     * Gets a list of nodes - if different (in value or reason) than the provided nodes
     * @param {Object[]} requiredNodes list of nodes required. If empty, all nodes are required
     * @param {boolean} [addHistory=true] true, if the history property will be added
     * @param {boolean} [addReason=true] true, if the reason property of the messages will be added
     */
    getNodes (requiredNodes, addHistory = true, addReason = true) {
        const result = { payload: [] }
        for (const requiredNode of requiredNodes) {
            const { topic, value, reason } = requiredNode
            const curNode = this.getNode(topic)
            if (curNode.value !== value || !this._isRequiredReason(reason, curNode.reason)) {
                const payloadNode = this._getPayload(curNode, addHistory, addReason)
                result.payload.push(payloadNode)
            }
        }
        return result
    }

    /**
     * @description Gets several levels of child nodes in an array of nodes
     * @param {string} topic start topic of the section
     * @param {number} levelAmount amount of levels to get
     * @param {Object[]} nodes list of nodes required. If empty, all nodes are required
     * @param {boolean} [addHistory=true] true, if the history property will be added
     * @param {boolean} [addReason=true] true, if the reason property of the messages will be added
     * @returns {{payload: Object[]}} Object with topics and payloads of all nodes in output format
     * for these (sub)topics
     */
    getSection (topic, levelAmount, addHistory = true, addReason = true) {
        const node = this.getNode(topic)
        const result = this._getSectionRec(node, levelAmount, addHistory, addReason)
        return result
    }

    /**
     * Removes all outdated nodes. An outdated node is a node with no update in the last days and without any 
     * child with an update in the last days
     * @param {number} daysWithoutUpdateToBeRemoved amount of days the last change is entered before a node is old 
     */
    cleanup(daysWithoutUpdateToBeRemoved) {
        if (Object.keys(this._tree.childs).length > 0) {
            this._tree = cleanup(this._tree, daysWithoutUpdateToBeRemoved)
            if (this._tree === null) {
                this._tree = { childs: {} }
            }
        }
    }
}

module.exports = MessageTree
