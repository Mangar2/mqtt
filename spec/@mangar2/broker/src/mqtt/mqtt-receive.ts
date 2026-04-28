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

const DEBUG = false;

import { Types, errorLog } from '@mangar2/utils';
import { IMessage, Message, qos_t, Interfaces } from '@mangar2/mqtt-utils';
import { MqttBase, IMqttConfiguration, HttpReturn } from './mqtt-base';
import { topics_t } from '../connections/client';
import { 
    checkConnectPayload, 
    checkDisconnectPayload,
    checkSubscribePayload,
    checkUnsubscribePayload
} from './checkdefinitions';


type headers_t = { [index: string]: string };

interface IMqttHeaders {
    qos: qos_t;
    dup: boolean;
    packetid: number;
    retain: boolean;
    version: string;
}

interface IConnectPayload {
    clientId: string;
    host: string;
    port: number;
    clean: boolean;
    keepAlive: number;
    password: string;
    user: string;
    will: IMessage;
}



export class MqttReceive extends MqttBase { 
    constructor(configuration: IMqttConfiguration) {     
        super(configuration);
    }

    /**
     * Translates http headers to mqtt headers
     * @param headers headers of the call
     * @returns parsed headers
     */
    private headersToMqttHeaders(headers: headers_t): IMqttHeaders {
        return {
            qos: headers.qos === '1' ? 1 : (headers.qos === '2' ? 2 : 0),
            dup: headers.dup === '1' || headers.dup === 'true',
            packetid: headers.packetid === undefined ? 0 : parseInt(headers.packetid),
            retain: headers.retain === '1' || headers.retain === 'true',
            version: headers.version || '0.0'
        }
    }

     /**
     * @private
     * @description
     * Receives a pubrel packages
     * @param {Object} payload publish payload
     * @param {Object} headers headers parameter {packetid}
     * @returns {Object} {headers, payload, statusCode}
     */
     onPubrel (payload: { token: string }, headers: headers_t) {
        const result = Interfaces.onPubrel(headers)
        this.connections.onPubrel(payload.token, result.packetid)
        return result
    }

    /**
     * @private
     * @description
     * Receives a message to pubish
     * @param {Object} payload publish payload
     * @param {Object} headers header parameters {qos, dup, packetid, retain}
     * @param {number} headers.qos quality of service
     * @param {(number|string)} [headers.dup] duplicate flag
     * @param {number} headers.packetid id of the packet
     * @param {(number|boolean)} [headers.retain] retain flag (true or 1 to retain the message)
     * @param {string} [headers.version='0.0'] interface version
     * @returns {httpReturn} http return information
     */
    onPublish (payload: IMessage | { message: IMessage, token: string }, headers: headers_t): HttpReturn {
        const token = 'token' in payload ? payload.token : undefined;

        const message = 'message' in payload ? payload.message: payload;
        const messageObj = Message.createMessage(message);
        messageObj.addReason('received by broker')

        let isDuplicate = false

        const { qos, dup, packetid, retain, version } = this.headersToMqttHeaders(headers)

        if (version !== '0.0' && token !== undefined) {
            const clientId = this.connections.checkToken(token)
            if (clientId !== undefined) {
                this.connections.getClientById(clientId).actualizeLastActiveTimestamp()
                isDuplicate = this.connections.isDuplicateQos2message(clientId, message.topic, qos, dup, packetid)
            }
        }

        const result = Interfaces.onPublish(headers)

        if (!isDuplicate) {
            this.publish(messageObj, dup, retain)
        }

        return result
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
    onConnect (payload: IConnectPayload, headers: headers_t): HttpReturn {
        const { version } = this.headersToMqttHeaders(headers)
        if (Types.isString(payload.port)) {
            payload.port = parseInt(payload.port)
        }
        checkConnectPayload.throwOnValidationError(
            payload, 'Connect wrong parameters (' + payload.clientId + '): ')
        const { clientId, host, port, clean, password, user, will } = payload
        let { keepAlive } = payload

        if (keepAlive === undefined && version === '0.0') {
            keepAlive = 0
        }
        if (!this.callbacks.invokeCallback('login', user, password)) {
            throw Error('Could not login')
        }
        const connect = this.connections.connect(clientId, host, port, clean, version, keepAlive)
        if (connect.mqttcode !== 0) {
            throw Error('Could not connect (code: ' + connect.mqttcode + ')')
        }
        const result = Interfaces.onConnect(headers, { present: connect.present, token: connect.token.send })
        this.connections.setWill(clientId, will)
        this.publishLogMessage('connect', 'success', 'client request', payload.clientId)
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
    onDisconnect (payload: { clientId: string }, headers: headers_t): HttpReturn {
        checkDisconnectPayload.throwOnValidationError(
            payload, 'Disconnect wrong parameters (' + payload.clientId + '): ')
        this.connections.disconnect(payload.clientId)
        this.publishLogMessage('disconnect', 'success', 'client request', payload.clientId)
        const result = Interfaces.onDisconnect(headers)

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
    onSubscribe (payload: { clientId: string, topics: topics_t, subscribe: topics_t }, headers: headers_t): HttpReturn {
        checkSubscribePayload.throwOnValidationError(
            payload, 'Subscribe wrong parameters (' + payload.clientId + '): ')

        const qos = this.connections.subscribe(
            payload.clientId,
            payload.subscribe === undefined ? payload.topics : payload.subscribe
        )
        this.publishLogMessage('subscribe', 'success', 'client request', payload.clientId)

        const result = Interfaces.onSubscribe(headers, qos)
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
    onUnsubscribe (payload: { clientId: string, topics: string[] }, headers: headers_t): HttpReturn {
        checkUnsubscribePayload.throwOnValidationError(
            payload, 'Unsubscribe wrong parameters (' + payload.clientId + '): ')

        this.publishLogMessage('unsubscribe', 'success', 'client request', payload.clientId)

        this.connections.unsubscribe(payload.clientId, payload.topics)
        const result = Interfaces.onUnsubscribe(headers)

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
    onPingreq (payload: { token: string }): HttpReturn {
        const client = this.connections.getClientByToken(payload.token)
        if (client === undefined) {
            throw Error('pingreq with illegal token')
        }

        client.actualizeLastActiveTimestamp()
        this.publishLogMessage('ping', 'success', 'client request', client.clientId)

        const result = {
            headers: { 'Content-Type': 'application/json', packet: 'pingresp' },
            payload: '',
            statusCode: client.isConnected() ? 204 : 400
        }
        return result
    }

    /**
     * @description
     * Calls a connection interface
     * @param {string} controls name of the mqtt controls to call (connect, disconnect, ...)
     * @param {Object} payload parameters for the interface (see interfaces)
     * @param {Object} headers headers of the http message
     * @returns {httpReturn} http return information
     */
    processRequest (controls: string, payload: any, headers:headers_t): HttpReturn {
        let result: HttpReturn = { headers: { 'Content-Type': 'text/plain' }, payload: 'request path not found: ' + controls, statusCode: 404 }
        try {
            switch (controls) {
            case 'publish': result = this.onPublish(payload, headers); break
            case 'pubrel': result = this.onPubrel(payload, headers); break
            case 'connect': result = this.onConnect(payload, headers); break
            case 'disconnect': result = this.onDisconnect(payload, headers); break
            case 'subscribe': result = this.onSubscribe(payload, headers); break
            case 'unsubscribe': result = this.onUnsubscribe(payload, headers); break
            case 'pingreq': result = this.onPingreq(payload); break
            case 'log': result = this.setLog(payload); break
            }
        } catch (err) {
            let error = 'Unknown error'
            if (err instanceof Error) {
                error = controls + ' ' + err.message
                errorLog(err, DEBUG);
            } else if (typeof (err) === 'string') {
                error = controls + ' ' + err
                errorLog(err, DEBUG);
            }
            this.publishLogMessage(controls, 'fail', 'client request', payload.clientId)
            result = {
                headers: { 'Content-Type': 'application/json' },
                payload: JSON.stringify({ error }),
                statusCode: 400
            }
        }
        return result
    }
}