/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Provides functions to connect to the (http based) yaha mqtt broker
 */

import { Types } from '@mangar2/utils';
import { HttpServer, IncomingHttpHeaders } from '@mangar2/httpservice';
import { OnPublish } from '../service/on-publish';
import { HttpOnConnect } from '../http-services/http-on-connect';
import { IMqttServer } from '../service/imqtt-server';

export type headers_t = IncomingHttpHeaders;

/**
 * Provides functions to receive data
 */
export class HttpReceiveServices {
    private _httpServer: HttpServer;
    private _onPublish: OnPublish;
    private _onConnect: HttpOnConnect | null = null;

    /**
     * Creates a new http service
     * @param port the port on which the server listens
     */
    constructor(port: number, qos2PubrelTimeoutInSeconds: number = 7200) {
        this._httpServer = new HttpServer(port);
        this._onPublish = new OnPublish(qos2PubrelTimeoutInSeconds);
        this._registerPut();
    } 

    /**
     * Enables handling of connect requests
     * @param mqttServer the mqtt server
     * @returns {void}
     */
    setMqttServer(mqttServer: IMqttServer) {
        this._onConnect = new HttpOnConnect(mqttServer);
        this._onPublish.on('publish', (message, dup) => { mqttServer.publish({ message, dup }); });  
    }

    /**
     * Listen for incoming http requests, e.g. from the broker
     * Sets the port used by the server
     */
    listen() {
        this._httpServer.listen();
    }

    /**
     * Closes the server
     */
    close() {
        return this._httpServer.close();
    }   

    /**
     * Registers the post method for the server
     * If the request is a publish request, the request is handled by the onPublish object
     * @private
     * @returns {void}
     */
    private _registerPut() {
        this._httpServer.on('PUT', (payload, headers, path, res) => {
            try {
                if (this._onPublish !== null && this._onPublish.isPublishRequest(path)) {
                    const result = this._onPublish.handleRequest(path, headers, payload);
                    res.writeHead(result.statusCode, result.headers);
                    res.end(result.payload);
                } else if (this._onConnect !== null && this._onConnect.isConnectRequest(path)) {
                    const result = this._onConnect.handleHttpRequest(path, headers, payload);
                    res.writeHead(result.statusCode, result.headers);
                    res.end(result.payload);
                } else {
                    res.writeHead(404, { 'Content-Type': 'text/plain' });
                    res.end('Illegal interface ' + path);
                }
            } catch (error) {
                res.writeHead(500, { 'Content-Type': 'text/plain' });
                res.end(Types.getType(error) === 'Error' ? (error as Error).message: 'Unknown error');
            }
        });
    }

    /**
     * Gets the port used by the server
     * It is valid only after the listen method has been called
     * @returns {number} the port used by the server
     */
    get port(): number {
        return this._httpServer.address?.port || 0;
    }

    /**
     * Gets the onPublish object
     * @returns {OnPublish} the onPublish object, if enabled
     * @throws {Error} if onPublish is not enabled
     */
    get onPublish(): OnPublish {
        if (this._onPublish === null) {
            throw new Error('onPublish not enabled');
        }
        return this._onPublish;
    }

    /**
     * Gets the onConnect event handler.
     * @returns The onConnect event handler.
     * @throws Error if onConnect is not enabled.
     */
    get onConnect(): HttpOnConnect {
        if (this._onConnect === null) {
            throw new Error('onConnect not enabled');
        }
        return this._onConnect;
    }   

}
