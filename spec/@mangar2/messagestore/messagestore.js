/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Provides a storage for Message trees and provides a rest interface to get chunks of the message
 * tree
 */

'use strict'

const Persist = require('@mangar2/persist')
const MessageTree = require('@mangar2/messagetree')
const HttpServer = require('@mangar2/httpservice').HttpServer
const sanitizeConfiguration = require('./configuration')
const { errorLog, types } = require('@mangar2/utils')

const DEBUG = false

/**
 * Creates a message store object and configures it. Adds a listener for get requests
 * @param {Object} [options={}] message store parameters
 */
class MessageStore {
    constructor (options = {}) {
        this.options = sanitizeConfiguration(options)
        this._subscriptions = options.subscriptions
        this.tree = new MessageTree(options.tree)
        this.tree.readTreeFromPersistedFile(options.persist.directory, options.persist.filename)
        this.httpServer = new HttpServer(options.server.port)
        this.isRunning = false
    }

    /**
     * Gets the required subscriptions for the Messagestore
     * @returns {Object} topic pattern: QoS value key/value object
     */
    getSubscriptions () {
        return this._subscriptions
    }

    /**
     * @private
     * Persists the data
     */
    async _persist () {
        while (this.isRunning) {
            await this.tree.persist(this.options.persist.directory, this.options.persist.filename)
            await Persist.delay(this.options.persist.interval)
        }
    }

    /**
     * @private
     * Retrieves a section of the message tree
     * @param {string} topic topic of the section
     * @param {Object} headers header information
     * @param {string} payload payload
     */
    _getSection (topic, headers, payload) {
        let section = []
        try {
            const levelAmount = isNaN(headers.levelamount) ? 1 : Number(headers.levelamount)
            const addHistory = headers.history === 'true'
            const addReason = !headers.reason || (headers.reason === 'true')
            let nodes = []
            if (types.isString(payload) && payload !== '') {
                nodes = JSON.parse(payload)
            }
            if (types.isArray(nodes) && nodes.length > 0) {
                section = this.tree.getNodes(nodes, addHistory, addReason)
            } else {
                section = this.tree.getSection(topic, levelAmount, addHistory, addReason)
            }
        } catch (err) {
            errorLog(err, DEBUG)
        }
        return section
    }

    /**
     * Listens to a configured port
     */
    run () {
        this.httpServer.on('GET', (payload, headers, path, res) => {
            const pathArray = path.split('/')
            if (pathArray[1] === this.options.server.path) {
                let topic = pathArray.slice(2).join('/')
                topic = topic.split('%20').join(' ')
                const resultPayload = this._getSection(topic, headers, payload)
                const resultHeaders = {
                    'content-type': 'application/json; charset=UTF-8'
                }
                res.writeHead(200, resultHeaders)
                res.end(JSON.stringify(resultPayload))
            } else {
                throw Error('unknown path ' + path)
            }
        })

        this.httpServer.listen()
        this.isRunning = true
        if (this.options.persist.interval !== 0) {
            this._persist()
        }
    }

    /**
     * Closes the broker, stops listening
     */
    async close () {
        this.isRunning = false
        await this.httpServer.close()
        if (this.options.persist.interval !== 0) {
            await this._persist()
        }
    }

    /**
     * Adds a message to the tree node matching to the topic path.
     * @param {Message} message {topic, value, reason}
     * @param {string} message.topic topic string
     * @param {Object} message.value value of the topic
     * @param {Object[]} message.reason list of reasons
     */
    handleMessage (message) {
        if (message.topic === '$SYS/messages/cleanup') {
            this.tree.cleanup(message.value)
        }
        this.tree.addData(message)
    }

    /**
     * Tree of messages
     * @type {MessageTree}
     */
    get messages () { return this._messages }
    set messages (messages) { this._messages = messages }
}

module.exports = MessageStore
