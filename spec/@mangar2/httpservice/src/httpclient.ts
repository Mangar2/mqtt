/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @brief http/https client to send data to a server
 */

import { Agent, IncomingHttpHeaders, request as httpRequest } from 'http';
import { request as httpsRequest } from 'https';
import { Client, SendOptions, ServiceType, Response } from './client';


/**
 * HttpClient is a wrapper class that manages Http and HttpS connections
 * using a Client instance. It provides methods to send various types of Http
 * requests and handle connections.
 */
export class HttpClient {
    private _service: ServiceType;
    private _client: Client;

    /**
     * Constructs an HttpClient instance.
     * @param {string} host The host to connect to.
     * @param {string | number} port The port to connect to.
     * @param {string} [type='http'] The type of service ('http' or 'https'). Defaults to 'http'.
     */
    constructor(host: string, port: string | number, type: string = 'http') {
        this._service = type === 'https' ? httpsRequest : httpRequest;
        this._client = new Client(host, port);
    }

    /**
     * Sets the agent for the HttpClient.
     * @param {Agent} agent The agent to use for connections.
     */
    set agent(agent: Agent | undefined) {
        this._client.agent = agent;
    }

    /**
     * Gets the agent used by the HttpClient.
     * @return {Agent} The current agent.
     */
    get agent(): Agent | undefined {
        return this._client.agent;
    }

    /**
     * Sets a new connection for the HttpClient.
     * @param {string} host The new host.
     * @param {string | number} port The new port.
     */
    setConnection(host: string, port: string | number): void {
        this._client.setConnection(host, port);
    }

    /**
     * Sends a request to the specified path with the given method, payload, and headers.
     * @param {string} path The path to send the request to.
     * @param {string} method The Http method to use.
     * @param {string | object} payload The payload to send.
     * @param {object} [headers={}] Optional headers to include in the request.
     * @return {Promise<Response>} The response from the request.
     */
    send(path: string, method: string, payload: string | object, headers: IncomingHttpHeaders): Promise<Response> {
        return this._client.send(path, method, payload, headers, this._service);
    }

    /**
     * Sends a request with options object.
     * @param {SendOptions} sendOptions The options for the request.
     * @return {Promise<Response>} The response from the request.
     */
    sendv2(sendOptions: SendOptions): Promise<Response> {
        return this._client.sendv2(sendOptions, this._service);
    }

    /**
     * Sends a POST request.
     * @param {Omit<SendOptions, 'method'>} sendOptions The options for the POST request.
     * @return {Promise<Response>} The response from the POST request.
     */
    post(sendOptions: Omit<SendOptions, 'method'>): Promise<Response> {
        return this.sendv2({ ...sendOptions, method: 'POST' });
    }

    /**
     * Sends a PUT request.
     * @param {Omit<SendOptions, 'method'>} sendOptions The options for the PUT request.
     * @return {Promise<Response>} The response from the PUT request.
     */
    put(sendOptions: Omit<SendOptions, 'method'>): Promise<Response> {
        return this.sendv2({ ...sendOptions, method: 'PUT' });
    }

    /**
     * Sends a GET request.
     * @param {string} path The path for the GET request.
     * @param {object} [headers={}] Optional headers for the request.
     * @return {Promise<Response>} The response from the GET request.
     */
    getRequest(path: string, headers: IncomingHttpHeaders = {}): Promise<Response> {
        return this.send(path, 'GET', '', headers);
    }

    /**
     * Closes the Http client connection.
     * @return {Promise<void>} A promise that resolves when the connection is closed.
     */
    close(): Promise<void> {
        return this._client.close();
    }
}
