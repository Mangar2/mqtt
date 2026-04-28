/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */
import http from 'http';
import Server from './server';
import { AddressInfo } from 'net';

export type ServerResponse = http.ServerResponse
export type IncomingHttpHeaders = http.IncomingHttpHeaders

type HttpCallback = (payload: string, headers: IncomingHttpHeaders, path: string, res: ServerResponse) => void;

/**
 * Represents an Http server for handling Http requests.
 */
export class HttpServer {
    private _server: Server;

    /**
     * Initializes a new instance of the HttpServer.
     * @param {number} port The port on which the server will listen.
     */
    constructor(port: number) {
        this._server = new Server(port);
    }

    /**
     * Gets the address information of the server.
     * @returns { AddressInfo | undefined } Address information or undefined if server is not started.
     */
    get address(): AddressInfo | undefined {
        const addressInfo = this._server.server?.address();
        if (typeof addressInfo === 'string' || addressInfo === null || addressInfo === undefined) {
            return undefined;
        }
        return addressInfo;
    }

    /**
     * Registers an event callback for the server.
     * @param {string} event The name of the event (e.g., 'GET', 'POST').
     * @param {HttpCallback} callback The callback function to handle the event.
     */
    on(event: string, callback: HttpCallback): void {
        this._server.on(event, callback);
    }

    /**
     * Starts the Http server to listen for requests.
     */
    listen(): void {
        this._server.listen(false);
    }

    /**
     * Stops the server from accepting new connections and closes existing ones.
     * @returns {Promise<void>} A promise that resolves when the server is closed.
     */
    close(): Promise<void> {
        return this._server.close();
    }
}
