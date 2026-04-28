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

const HttpsClient = require('@mangar2/httpservice').HttpsClient
const Message = require('@mangar2/message')
const querystring = require('querystring')
const CheckInput = require('@mangar2/checkinput')

/**
 * Check input parameters
 * @private
 */
const checkOptions = new CheckInput({
    type: 'object',
    properties: {
        host: { type: 'string' },
        port: { type: 'number' },
        user: { type: 'string' },
        password: { type: 'string' },
        paths: {
            type: 'object',
            properties: {
                login: { type: 'string' },
                production: { type: 'string' }
            },
            required: ['login', 'production']
        }
    },
    required: ['host', 'user', 'password', 'paths']
})

/**
 * Store for cookies
 * @private
 */
class Cookies {
    constructor () {
        this._cookies = {}
    }

    /**
     * Parse Cookie
     * @param {string} cookie cookie string
     * @private
     */
    _parseCookie (cookie) {
        const parts = cookie.split(';')
        const result = {}
        let index = 0
        for (const part of parts) {
            const keyVal = part.split('=')
            if (keyVal[1] === undefined) {
                keyVal[1] = ''
            }
            if (index === 0) {
                result.name = keyVal[0]
                result.value = keyVal[1]
            } else {
                result[keyVal[0]] = keyVal[1]
            }
            index++
        }
        return result
    }

    /**
     * Sets cookies
     * @param {string[]} cookies cookie strings
     */
    setCookies (cookies) {
        for (const cookie of cookies) {
            const cookieObj = this._parseCookie(cookie)
            this._cookies[cookieObj.name] = cookieObj.value
        }
    }

    /**
     * Gets cookies as string
     * @returns {string} cookie string
     */
    getCookieString () {
        let result = ''
        let spacer = ''
        for (const name in this._cookies) {
            const value = this._cookies[name]
            result = result + spacer + name + '=' + value
            spacer = '; '
        }
        return result
    }
}

/**
 * Calculates the average of an array of numbers
 * @param {number[]} numbers array of numbers
 * @private
 */
function average (numbers) {
    return numbers.reduce((a, b) => a + b) / numbers.length
}

/**
 * Manages a list of messages
 * @private
 */
class MessageList {
    constructor () { this.messages = [] }

    /**
     * messages array of messages
     * @type {Message[]}
     */
    get messages () { return this._messages }
    set messages (messages) { this._messages = messages }

    /**
     * Adds a message, if the value is defined
     * @param {string} topic message topic
     * @param {number} value message value
     * @param {Date} timestamp timestamp of the sunny measurement
     * @returns {Message[]} array of messages with one more message (if value is defined)
     */
    addMessage (topic, value, timestamp, retain = true, qos = 0) {
        if (value !== undefined && value !== null && value !== '') {
            const message = new Message(topic, value, 'read from sunny portal', timestamp)
            message.qos = qos
            message.retain = retain
            this.messages.push(message)
        }
    }
}

/**
 * Gets current production/consumption infos from sunny portal
 * Constructs a new sunny portal object
 * @param {Object} config configuration options
 * @param {string} config.host host of open sense map
 * @param {port} [config.port=443] portnumer of open sense map (usually 443)
 * @param {string} config.user name of the login user
 * @param {string} config.password login password
 * @param {Object} config.paths needed paths to access the specific parts of the page
 * @example
 * const config = {
 *   user: 'your user',
 *   password: 'your password',
 *   host: 'www.sunnyportal.com',
 *   port: 443,
 *   paths: {
 *       login: '/Templates/Start.aspx',
 *       logout: '/Templates/Logout.aspx',
 *       production: '/Dashboard?_=1'
 *   }
 * }
 * const sunnyportal = new SunnyPortal(config)
 * const messages = sunnyportal.getMessages()
 * console.log(messages)
 */
class SunnyPortal {
    constructor (config) {
        if (checkOptions.validate(config) !== true) {
            throw new Error('SunnyPortal:' + JSON.stringify(checkOptions.messages, null, 2))
        }
        const { host, port = 443, user, password } = config
        this._user = user
        this._password = password
        this._paths = config.paths
        this._client = new HttpsClient(host, port)
        this._cookies = new Cookies()
        this._loggedin = false
        this._topicRoot = config.topicRoot
        this._feedInHistory = []
    }

    /**
     * Calculates an average of the last five feed in values by keeping a history
     * @param {number} feedIn current feed in power
     * @private
     */
    averageFeedIn (feedIn) {
        this._feedInHistory.push(feedIn === null ? 0 : feedIn)
        if (this._feedInHistory.length > 5) {
            this._feedInHistory.shift()
        }
        return average(this._feedInHistory)
    }

    /**
     * Loggs in to the sunny portal
     * @private
     */
    async _login () {
        const headers = {
            'content-type': 'application/x-www-form-urlencoded',
            'user-agent': 'Mozilla/5.0 (node.js)',
            'accept-encoding': 'identity',
            accept: 'text/html'
        }

        const form = {
            __EVENTTARGET: '',
            ctl00$ContentPlaceHolder1$Logincontrol1$LoginBtn: 'Login',
            ctl00$ContentPlaceHolder1$Logincontrol1$txtPassword: this._password,
            ctl00$ContentPlaceHolder1$Logincontrol1$txtUserName: this._user,
            ctl00$ContentPlaceHolder1$Logincontrol1$ServiceAccess: true,
            ctl00$ContentPlaceHolder1$hiddenLanguage: 'de-de'
        }
        const payload = querystring.stringify(form)
        const result = await this._client.send(this._paths.login, 'POST', payload, headers)
        this._cookies.setCookies(result.headers['set-cookie'])
        console.log(result)
        const success = result.headers.location === '/FixedPages/HoManLive.aspx'
        console.log(success ? 'Login successful' : 'Login not successful')
        return success
    }

    /**
     * Returns the current production at this moment in time.
     * @method currentProduction
     * @param {Function} callback A callback function once current production is recieved.  Will return a JSON object of the current status.
     * @private
     */
    async _readProduction () {
        let resultData = {}
        if (!this._loggedin) {
            this._loggedin = await this._login()
        }
        if (this._loggedin) {
            const headers = {
                'content-type': 'test/plain',
                'accept-encoding': 'identity',
                Accept: 'application/json',
                Cookie: this._cookies.getCookieString()
            }
            const payload = ''
            const result = await this._client.send(this._paths.production, 'GET', payload, headers)

            if (result.statusCode === 200 && result.headers['content-type'].startsWith('application/json')) {
                resultData = JSON.parse(result.payload)
            } else {
                this._loggedin = false
            }
        }
        return resultData
    }

    /**
     * Creates messages from sunny portal data
     * @param {Object} data data read from sunny portal
     * @returns {Message[]} Messages to send
     * @private
     */
    _createMessages (data) {
        const messages = new MessageList()
        // Do not send data older than 5 minutes

        const dateTime = data.Timestamp.DateTime
        const dataTimestamp = new Date(dateTime)
        const consumption = data.TotalConsumption ? data.TotalConsumption : data.GritConsmptions
        const averageFeedIn = this.averageFeedIn(data.FeedIn)

        messages.addMessage(this._topicRoot + 'info', data.InfoMessages.join('; '))
        messages.addMessage(this._topicRoot + 'warning', data.WarningMessages.join('; '))
        messages.addMessage(this._topicRoot + 'error', data.ErrorMessages.join('; '))

        messages.addMessage(this._topicRoot + 'consumption', consumption, dataTimestamp)
        messages.addMessage(this._topicRoot + 'solar yield', data.PV, dataTimestamp)
        messages.addMessage(this._topicRoot + 'feedin', data.FeedIn, dataTimestamp)
        messages.addMessage(this._topicRoot + 'average feedin', averageFeedIn, dataTimestamp)
        return messages
    }

    /**
     * Gets messages to send to the client
     * @returns {Message[]} Messages to send
     */
    async processTasks () {
        const RETRY = 2
        let data = {}
        let messages = new MessageList()

        for (let count = 0; count < RETRY; count++) {
            data = await this._readProduction()
            if (this._loggedin) {
                break
            }
        }

        if (!data.Timestamp || !data.Timestamp.DateTime) {
            this._loggedin = false
        } else {
            const dataTimestamp = new Date(data.Timestamp.DateTime).getTime()
            const now = new Date().getTime()
            if (now - dataTimestamp > 5 * 60 * 1000) {
                // if data is too old we are probably logged out
                this._loggedin = false
            }
        }
        if (this._loggedin) {
            messages = this._createMessages(data)
        }
        return messages.messages
    }
}

module.exports = SunnyPortal
