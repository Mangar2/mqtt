/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Provides functionalities to communicate with an mqtt broker over http
 */

import os from 'os';

import { Types } from '@mangar2/utils';
import { SubscribeResult, UnsubscribeResult, ConnectResult, topics_t, IMessage, Logger, Interfaces } from '@mangar2/mqtt-utils';

import { IMqttClientServices } from '../mqtt-service/imqtt-client-services';
import { HttpReceiveServices, HttpSendServices } from '../index';
import { BrokerOptions } from '../service/types';

export type version_t = '0.0' | '1.0';

/**
 * @private
 * @description
 * Gets the IP v4 address of the current device
 * @returns {string} IP v4 address
 */
export function getIPv4Address(): string {
    const networkInterfaces = os.networkInterfaces();
    for (const network of Object.values(networkInterfaces)) {
        if (!network) continue;
        for (const info of network) {
            if (info.family === 'IPv4' && !info.internal) {
                return info.address;
            }
        }
    }
    return 'localhost';
}

/**
 * Represents the options for configuring an MQTT client.
 */
export type HttpClientOptions = {
    /**
     * The client ID used to identify the MQTT client.
     */
    clientId: string;
    /**
     * The options for configuring the MQTT broker.
     */
    brokerOptions: BrokerOptions;
    /**
     * The listener port number for the MQTT client.
     */
    listener?: number;
    /**
     * The version of the MQTT protocol to use.
     */
    version?: version_t;
}

export class HttpClient implements IMqttClientServices {

    private _server: HttpReceiveServices;
    private _client: HttpSendServices;

    private _version: string;
    private _clientId: string;

    private nextPacketId: number = 0;

    constructor(private options: HttpClientOptions) {

        const { clientId, brokerOptions, listener, version } = options;
        this._server = new HttpReceiveServices(listener || 0);
        this._client = new HttpSendServices(brokerOptions);

        this._version = version || '1.0';
        this._clientId = clientId;
    }

    /**
     * @private
     * @description
     * Provides a packet id for the client
     * @returns {number} packet id
     */
    private providePacketId(): number {
        return this.nextPacketId = (this.nextPacketId % 0xFFFF) + 1;
    }

    /**
     * Checks if the MQTT client supports a specific feature.
     * @param feature - The feature to check.
     * @returns True if the client supports the feature, false otherwise.
     */
    supportsFeature(feature: string): boolean {
        if (feature === 'pingreq') {
            return this.options.version !== '0.0';
        } else if (feature in ['connect', 'disconnect', 'subscribe', 'publish']) {
            return true;
        } else {
            return false;
        }
    }

    /**
     * @private
     * @description
     * Extracts the error message from an error
     * @param {string|Error|any} err 
     * @returns {string} error message
     */
    private getMessage(err: string | Error | any): string {
        return Types.isString(err) ? err : Types.isError(err) ? err.message : 'unknown error';
    }


    /**
     * Connects to the MQTT server.
     * 
     * @param clean - A boolean indicating whether to clean the session or not.
     * @returns A Promise that resolves to a ConnectResult object.
     * @throws An error if unable to connect.
     */
    async connect(clean: boolean): Promise<ConnectResult> {
        try {
            const sendData = Interfaces.connect(this._version,  { 
                clientId: this._clientId, 
                clean, 
                port: this._server.port,
                host: getIPv4Address()
            });
            const result = await this._client.send('/connect', sendData.headers, sendData.payload);
            sendData.resultCheck(result);
            const { } = result;
            // resultCheck checked, that content type is application/json. So parsing should be ok.
            const parsedResult = JSON.parse(result.payload);
            const { present, token, mqttcode } = parsedResult;
            Logger.logger.log(
                'out',
                `$SYS/${this._clientId}/connected`,
                `connected, ${present ? 'Session available, ' : ''} send-token:${token.send}, receive-token:${token.receive} ${mqttcode ? `, mqttcode: ${mqttcode}` : ''}`,
                0);
            return parsedResult;
        } catch (err) {
            throw new Error(`Unable to connect: ${this.getMessage(err)}`)
        }
    }

    /**
     * Disconnects the MQTT client from the server.
     * @returns A promise that resolves when the client is successfully disconnected.
     * @throws An error if the disconnection fails.
     */
    async disconnect(): Promise<void> {
        try {
            const sendData = Interfaces.disconnect(this._version, this.options.clientId);
            this._client.send
            const result = await this._client.send('/disconnect', sendData.headers, sendData.payload);
            sendData.resultCheck(result)
            Logger.logger.log('out', `$SYS/${this._clientId}/disconnected`, `disconnected`);
        }
        catch (err) {
            throw new Error(`Unable to disconnect: ${this.getMessage(err)}`);
        }
    }

    /**
     * Subscribes to MQTT topics.
     * 
     * @param topics - The topics to subscribe to.
     * @returns A promise that resolves to a SubscribeResult object.
     * @throws An error if unable to subscribe.
     */
    async subscribe(topics: topics_t): Promise<SubscribeResult> {
        try {
            const sendData = Interfaces.subscribe(this._version, topics, this._clientId, this.providePacketId());
            const result = await this._client.send('/subscribe', sendData.headers, sendData.payload);
            sendData.resultCheck(result)
            Logger.logger.log('out', `$SYS/${this._clientId}/subscribe`, `subscribed successfully`);
            return JSON.parse(result.payload);
        } catch (err) {
            throw new Error(`Unable to subscribe: ${this.getMessage(err)}`);
        }
    }

    /**
     * Unsubscribes from MQTT topics.
     * 
     * @param topics - The topics to unsubscribe from.
     * @returns A promise that resolves to an UnsubscribeResult object.
     * @throws An error if unable to unsubscribe.
     */
    async unsubscribe(topics: topics_t): Promise<UnsubscribeResult> {
        try {
            const sendData = Interfaces.unsubscribe(this._version, topics, this._clientId, this.providePacketId());
            const result = await this._client.send('/unsubscribe', sendData.headers, sendData.payload);
            sendData.resultCheck(result)
            Logger.logger.log('out', `$SYS/${this._clientId}/unsubscribe`, `unsubscribed successfully`);
            return JSON.parse(result.payload);
        } catch (err) {
            throw new Error(`Unable to unsubscribe: ${this.getMessage(err)}`);
        }

    }

    /**
     * Sends a ping request to signal the client is alive
     * @returns {boolean} true on success
     */
    async pingreq(token: string): Promise<void> {
        try {
            const payload = { token };
            const headers = { 'content-type': 'application/json; charset=UTF-8' };
            const result = await this._client.send('/pingreq', headers, payload);
            if (result.statusCode !== 204) {
                throw new Error(`Illegal return code, expected 204 got ${result.statusCode}`);
            }
            if (result.headers.packet !== 'pingresp') {
                throw new Error(`Wrong packet id, expected \'pingresp\', got ${result.headers.packet}`);
            };
            Logger.logger.log('out', `$SYS/${this._clientId}/pingreq`, `pinged successfully`);
        } catch (err) {
            throw new Error(`Error in ping request: ${this.getMessage(err)}`);
        }
    }

    async publish(token: string, message: IMessage, serviceName: string): Promise<string[]> {
        return this._client.publish(token, message, serviceName, this.options.version);
    }

    onPublish(callback: (message: IMessage) => void): void {
        this._server.onPublish.on('publish', callback);
    }

    start(): void {
        this._server.listen();
    }

    close(): void {
        this._server.close();
    }

}