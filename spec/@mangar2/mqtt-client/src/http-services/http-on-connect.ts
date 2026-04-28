/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Helping functions to send mqtt data
 */

import CheckInput from '@mangar2/checkinput';
import { Types } from '@mangar2/utils';
import { IResult, Interfaces, ConnectOptions, SubscribeOptions, UnsubscribeOptions, Logger } from '@mangar2/mqtt-utils';
import { IMqttServer } from '../service/imqtt-server';
import { headers_t } from './http-receive-services';


/**
 * Handles incoming server requests of the connect process.
 */
export class HttpOnConnect {

    constructor(private mqttServer: IMqttServer, private logger: Logger | null = null) {
    }

    /**
     * Validates the connect parameter and returns the validated object
     * @param {string} payload payload provided for connect
     * @returns validated connection options
     */
    HttpToConnectOptions(headers: headers_t, payload: string): ConnectOptions {
        const result: ConnectOptions = JSON.parse(payload);
        const checkConnectPayload = new CheckInput({
            type: 'object',
            properties: {
                clientId: { type: 'string' },
                host: { type: 'string' },
                port: { type: ['integer'] },
                clean: { enum: ['true', 'false', true, false] },
                keepAlive: { type: ['number', 'string'] },
                password: { type: 'string' },
                user: { type: 'string' },
                will: {
                    type: 'object',
                    properties: ''
                }
            },
            required: ['clientId', 'host', 'port', 'clean']
        })
        checkConnectPayload.throwOnValidationError(result, 'Unable to connect: ');
        result.qos = headers.qos === '0' ? 0 : headers.qos === '2' ? 2 : 1;
        result.version = headers.version == '1.0' ? '1.0' : '0.0';
        return result;
    }

    /**
     * Validates the connect parameter and returns the validated object
     * @param {string} payload payload provided for connect
     * @returns validated connection options
     */
    HttpToSubscribeOptions(payload: string): SubscribeOptions {
        const result: SubscribeOptions = JSON.parse(payload);

        const checkSubscribePayload = new CheckInput({
            type: 'object',
            properties: {
                clientId: { type: 'string' },
                topics: {
                    type: 'object',
                    additionalProperties: {
                        type: 'integer',
                        enum: [0, 1, 2]
                    }
                },
                subscribe: {
                    type: 'object',
                    properties: {
                        QoS: {
                            enum: ['0', '1', '2', 0, 1, 2]
                        },
                        topics: {
                            type: ['string', 'array'],
                            items: { type: 'string' }
                        }
                    }
                }
            },
            required: ['clientId']
        })

        checkSubscribePayload.throwOnValidationError(result, 'Unable to subscribe: ');
        return result;
    }

    /**
     * Validates the connect parameter and returns the validated object
     * @param {string} payload payload provided for connect
     * @returns validated connection options
     */
    HttpToUnsubscribeOptions(payload: string): UnsubscribeOptions {
        const result: UnsubscribeOptions = JSON.parse(payload);

        const checkUnsubscribePayload = new CheckInput({
            type: 'object',
            properties: {
                clientId: { type: 'string' },
                topics: { type: 'array', items: { type: 'string' } }
            },
            required: ['clientId']
        })

        checkUnsubscribePayload.throwOnValidationError(result, 'Unable to unsubscribe: ');
        return result;
    }

    /**
     * Checks if the request is a connect request
     * @param {string} path the path of the request
     * @returns {boolean} true, if the request is a connect request
     */
    public isConnectRequest(path: string): boolean {
        return ['/connect', '/disconnect', '/subscribe', '/unsubscribe', '/pingreq'].includes(path);
    }

    /**
     * Handles incoming server requests.
     * @param {string} path - The request path.
     * @param {headers_t} headers - The headers of the request.
     * @param {string} payload - The payload of the request.
     * @param {ServerResponse} res - The response object.
     */
    public handleHttpRequest(path: string, headers: headers_t, payload: string): IResult {
        if (this.logger) {
            this.logger.log('in', path, 'received: ' + JSON.stringify(headers) + ' ' + payload)
        }
        switch (path) {
            case '/connect':
                return this.onConnect(headers, payload);
            case '/disconnect':
                return this.onDisconnect(headers, payload);
            case '/subscribe':
                return this.onSubscribe(headers, payload);
            case '/unsubscribe':
                return this.onUnsubscribe(headers, payload);
            case '/pingreq':
                return this.onPingreq(payload);
            default:
                throw new Error('Illegal interface ' + path);
        }
    }



    /**
     * @private
     * @description
     * Connects a client to the broker, storing the connection informations
     * @param {Object} payload payload {clientId, host, port, clean, will}
     * @param {string} payload.clientId id of the connecting client
     * @param {string} payload.host host name (or ip) of the client
     * @param {number} payload.port port number
     * @param {boolean} payload.clean true, if the session shall be cleaned
     * @param {number} payload.keepAlive timeout in milliseconds for disconnecting
     * if no message is transmitted
     * @param {string} payload.password connection password
     * @param {string} payload.user connection user name
     * @param {IMessage} payload.will message to be send on connection loss
     * @param {Object} headers received headers
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    private onConnect(headers: headers_t, payload: string): IResult {
        const connectOptions = this.HttpToConnectOptions(headers, payload);
        const connectResult = this.mqttServer.connect(connectOptions);
        const result = Interfaces.onConnect(headers, connectResult)
        return result
    }

    /**
     * @private
     * @description
     * disconnects a client from the broker
     * @param {Object} payload
     * @param {string} payload.clientId Id of the client to disconnect
     * @param {Object} headers received headers
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    private onDisconnect(headers: headers_t, payload: string): IResult {
        const disconnectOptions = JSON.parse(payload);
        if (!Types.isObject(disconnectOptions)) {
            throw new Error('Disconnect error: wrong payload format')
        }
        if (!Types.isString(disconnectOptions.clientId) || disconnectOptions.clientId === '') {
            throw new Error('Disconnect error: missing clientId');
        }
        this.mqttServer.disconnect(disconnectOptions);
        const result = Interfaces.onDisconnect(headers)

        // this.publishLogMessage('disconnect', 'success', 'client request', payload.clientId)
        return result
    }

    /**
     * @private
     * @description
     * Subscribes to topics
     * @param {Object} payload {clientId, topics}
     * @param {Object} headers received headers
     * @param {number} headers.packetid id of the packet
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    private onSubscribe(headers: headers_t, payload: string): IResult {
        const subscribeOptions = this.HttpToSubscribeOptions(payload);
        const subscribeResult = this.mqttServer.subscribe(subscribeOptions);
        const result = Interfaces.onSubscribe(headers, subscribeResult)
        return result
    }

    /**
     * @private
     * @description
     * Unsubscribes to topics
     * @param {Object} payload {clientId, topics}
     * @param {string} payload.clientId id of the connecting client
     * @param {string[]} payload.topics topics to subscribe to
     * @param {Object} headers received headers
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    private onUnsubscribe(headers: headers_t, payload: string): IResult {
        const unsubscribeOptions = this.HttpToUnsubscribeOptions(payload);
        const unsubscribeResult = this.mqttServer.unsubscribe(unsubscribeOptions);
        const result = Interfaces.onUnsubscribe(headers, unsubscribeResult)
        return result
    }

    /**
     * @private
     * @description
     * Answers to a ping
     * @param {Object} payload http call payload
     * @param {string} payload.token
     * @returns {httpReturn} http return information
     */
    private onPingreq(payload: string): IResult {
        const pingreqOptions: { token: string } = JSON.parse(payload);
        this.mqttServer.pingreq(pingreqOptions.token);

        const result = {
            headers: { 'Content-Type': 'application/json', packet: 'pingresp' },
            payload: '',
            statusCode: 204
        }
        return result
    }

}
