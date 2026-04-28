/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Provides a standard client to communicate with the mqtt broker
 */

import { Types, delay, shutdown, Callbacks, errorLog as ErrorLog } from '@mangar2/utils';
import {
    Message, IMessage,
    TopicMatch, Logger, createMemoryUsageMessage,
    SubscribeResult,
    topics_t
} from '@mangar2/mqtt-utils';
import { IMqttClientServices } from '../mqtt-service/imqtt-client-services';
import { IMqttClient, MqttClientOptions, receipientCallback_t, pollCallback_t } from './imqtt-client';


const DEBUG = true
const errorLog = (error: any) => { ErrorLog(error, DEBUG) }



export class MqttClient implements IMqttClient {

    private _clientId: string;
    private _clean: boolean;
    private _connected: boolean;
    private _isShuttingDown: boolean = false;
    private _token: { send: string, receive: string };
    private _callbacks: Callbacks;
    private _closeChain: (() => void)[];
    private _recipients: { [key: string]: { callback: receipientCallback_t, topics: topics_t } } = {}
    private _keepAlive: number;
    
    constructor(options: MqttClientOptions, private _mqttClientServices: IMqttClientServices) {
        if (options === undefined) {
            throw Error('MqttClient constructor needs a valid options object; Provided: undefined')
        }
        const { clientId, clean } = options
        this._clientId = clientId;
        this._clean = clean || true;
        this._connected = false;
        this._token = { send: '', receive: '' };
        this._closeChain = []
        this._recipients = {}
        this._isShuttingDown = false
        this.connected = false
        this._keepAlive = 10 * 60 * 1000; // 10 minutes

        if (Types.isNumber(options.keepAliveInSeconds)) {
            this._keepAlive = options.keepAliveInSeconds * 1000
        }
        this._callbacks = new Callbacks(['shutdown'])
        this._initCallbacks()
    }

    /**
     * Connection status. true, iff connected
     * @type {boolean}
     */
    get connected() { return this._connected }
    set connected(connected) { this._connected = connected }

    get isShuttingDown(): boolean {
        // Add your implementation here
        return this._isShuttingDown;
    }

    /**
     * Registers a callback for a specific event.
     * @param {string} event - The event name.
     * @param {ProcessMessage} callback - The callback to be invoked.
     */
    on(event: string, callback: () => void): void {
        this._callbacks.on(event, callback);
    }

    /**
     * @private
     * @description
     * Sets shutdown status. shutdown is cleared in the contructor and
     * set in the shutdown function.
     * @param {boolean} [isShuttingDown=true] true, iff shutting down
     */
    signalShutdown(isShuttingDown = true) { this._isShuttingDown = isShuttingDown }

    /**
     * Registeres a recipient
     * @param {string} serviceName name of the subscribing service
     * @param {Object} subscriptions subscription entries of format {topic:qos, topic:qos, ...}
     * @param {function} callback function to send received messages to
     * @returns {Promise<SubscribeResult>} result of the subscription
     * @throws {Error} If subscriptions are not well formatted or callback is not a function
     */
    async registerRecipient(serviceName: string, topics: topics_t, callback: receipientCallback_t): Promise<SubscribeResult> {
        if (!this.isShuttingDown) {
            try {
                if (!Types.isAnyFunction (callback)) {
                    throw Error('A callback function must be provided for registerRecipient')
                }
                this._recipients[serviceName] = { topics, callback }
                return await this.subscribe(serviceName, topics)
            } catch (error) {
                errorLog(error)
            }
        }
        return { qos: [127] };
    }

    /**
     * @private
     * @description
     * Regularly calls a function
     * @param {number} intervalInMilliseconds interval between calls in milliseconds
     * @param {funnction} callback function to call
     */
    private async invokeCallback(intervalInMilliseconds: number, callback: () => void) {
        while (!this.isShuttingDown) {
            try {
                if (this.connected) {
                    await callback()
                }
            } catch (err) {
                errorLog(err)
            }
            await delay(intervalInMilliseconds)
        }
    }

    /**
     * Registers a sender function that will be invoked periodically.
     * 
     * @param intervalInMilliseconds - The interval in milliseconds at which the sender function will be invoked.
     * @param callback - The callback function that will be invoked to retrieve the messages to be sent.
     * @throws {Error} - If the callback parameter is not a function.
     */
    async registerSender(intervalInMilliseconds: number, callback: pollCallback_t) {
        if (!Types.isAnyFunction(callback)) {
            throw Error('A callback function must be provided for registerSender')
        }
        this.invokeCallback(intervalInMilliseconds, async () => {
            let messages = await callback()
            if (messages instanceof Message) {
                messages = [messages]
            }
            if (Array.isArray(messages)) {
                for (const message of messages) {
                    await this.publish(message)
                }
            }
        })
    }

    /**
     * Updates the subscriptions
     * @param {string} serviceName name of the subscribing service
     * @param {Object} subscriptions subscription entries of format {topic:qos, topic:qos, ...}
     * @throws {Error} If subscriptions are not well formatted 
     * @returns {Promise<SubscribeResult>} result of the subscription
     */
    async subscribe(serviceName: string, topics: topics_t): Promise<SubscribeResult> {
        if (this._recipients[serviceName] === undefined) {
            throw new Error(`Service ${serviceName} not registered`);
        }
        if (!this.isShuttingDown) {
            this._recipients[serviceName].topics = topics
            if (this.connected) {
                return await this._mqttClientServices.subscribe(topics)
            }
        }
        return { qos: [127] };
    }


    /**
     * Publishes a message to the MQTT broker.
     * @param message - The message to be published.
     * @param serviceName - The name of the service.
     * @returns A promise that resolves to an array of strings representing the published message.
     */
    async publish(message: IMessage, serviceName: string = ''): Promise<string[]> {
        const token = this._token ? this._token.send : 'undefined'
        return this._mqttClientServices.publish(token, message, serviceName)
    }

    /**
     * @private
     * @description
     * Initializes the callbaks for "on publish" and "shutdown"
     */
    _initCallbacks() {
        this._mqttClientServices.onPublish(async (message: IMessage) => {
            for (const key in this._recipients) {
                const recipient = this._recipients[key]
                const topicMatch = new TopicMatch(recipient.topics)
                if (topicMatch.getFirstMatch(message.topic) !== undefined) {
                    const messageCopy = message.clone()
                    let returnMessage = await recipient.callback(messageCopy)
                    if (returnMessage instanceof Message) {
                        returnMessage = [returnMessage]
                    }
                    if (Array.isArray(returnMessage)) {
                        for (const entry of returnMessage) {
                            this.publish(entry)
                        }
                    }
                }
            }
        })

        shutdown(async () => {
            await this.close()
        })

        this.on('shutdown', () => { })
    }

    /**
     * Connects and subscribes to the broker
     */
    async reconnect() {
        try {
            const result = await this._mqttClientServices.connect(this._clean);
            this.connected = true
            this._token = result.token
            for (const key in this._recipients) {
                const recipient = this._recipients[key]
                const subscribeResult = await this._mqttClientServices.subscribe(recipient.topics)
                const errorPosition = subscribeResult ? subscribeResult.qos.indexOf(127) : -1
                if (errorPosition > -1) {
                    throw 'subscribe failed at postion: ' + errorPosition
                }
            }
        } catch (error) {
            this.connected = false
            errorLog(error)
        }
    }

    /**
     * @private
     * @description
     * Ensures that the system stayes connected with the broker
     * @param {number} memoryUsageTicks number of loops between memory usage messages. No message is sent, if
     * this parameter is <= 0
     */
    async _keepConnected(memoryUsageTicks: number) {
        let memoryUsageMessageCountdown = 0
        while (!this.isShuttingDown) {
            try {
                if (!this._mqttClientServices.supportsFeature('pingreq') || !this.connected) {
                    await this.reconnect()
                } else {
                    await this._mqttClientServices.pingreq(this._token.send)
                }
                if (memoryUsageMessageCountdown <= 0) {
                    if (memoryUsageTicks > 0) {
                        this.publish(createMemoryUsageMessage(this._clientId))
                    }
                    memoryUsageMessageCountdown = memoryUsageTicks
                }
                memoryUsageMessageCountdown--
            } catch (error) {
                this.connected = false
                errorLog(error)
            }
            Logger.logger.printLog();
            Logger.logger.clearLog();
            let delayInMs = this.connected ? this._keepAlive : 1000
            const step = 200
            for (; delayInMs > 0 && !this.isShuttingDown; delayInMs -= step) {
                await delay(step)
            }
        }
    }

    /**
     * Starts the mqttclient. Opens the listener and connects to the broker
     * @param {number} [memoryUsageLoopNumber=10] number of ping-loops between memory usage messages. No message is sent, if
     * this parameter is <= 0. A ping-loop takes keepAliveInSeconds/3 seconds
     */
    async run(memoryUsageLoopNumber = 10) {
        if (!this.isShuttingDown) {
            try {
                this._mqttClientServices.start();
                await this.reconnect()
                this._keepConnected(memoryUsageLoopNumber)
            } catch (error) {
                errorLog(error)
            }
        }
    }

    /**
     * closes the client by shutting down all services and loops
     */
    async close() {
        try {
            this.signalShutdown()
            await this._callbacks.invokeCallbackAsync('shutdown')
            await delay(500)
            for (const closeFunction of this._closeChain) {
                await closeFunction()
            }
            await this._mqttClientServices.disconnect()
        } catch (error) {
            errorLog(error)
        }
        await this._mqttClientServices.close()
    }

    /**
     * Registers close functions. It will be called when the client close function is called
     * @param {function} closeFunction function to be called on close commands
     */
    registerCloseFunction(closeFunction: () => void) {
        if (Types.isAsyncFunction(closeFunction)) {
            this._closeChain.push(closeFunction)
        }
    }
}