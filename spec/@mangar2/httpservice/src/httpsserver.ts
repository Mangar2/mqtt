/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import https from 'https';
import http from 'http';
import Server from './server';
import { AddressInfo } from 'net';

type HttpCallback = (payload: string, headers: http.IncomingHttpHeaders, path: string, res: http.ServerResponse) => void;

/**
 * Represents an HttpS server for handling Http requests securely.
 */
export class HttpsServer {
    private _server: Server;
    private _options: { key: string; cert: string };

    /**
     * Initializes a new instance of the HttpSServer.
     * @param {number} port The port on which the server will listen.
     * @param {string} key The SSL key for HttpS.
     * @param {string} cert The SSL certificate for HttpS.
     */
    constructor(port: number, key: string, cert: string) {
        this._options = { key, cert };
        this._server = new Server(port);
    }

    /**
     * Gets the address information of the server.
     * @returns {AddressInfo | undefined} Address information or undefined if server is not started.
     */
    get address(): AddressInfo | undefined {
        const addressInfo = this._server.server?.address();
        if (typeof addressInfo === 'string' || addressInfo === null || addressInfo === undefined) {
            return undefined;
        }
        return addressInfo as AddressInfo;
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
     * Starts the HttpS server to listen for requests.
     * @param {https.ServerOptions} options The HttpS server options.
     */
    listen(options: https.ServerOptions = this._options): void {
        this._server.listen(true, { ...this._options, ...options });
    }

    /**
     * Stops the server from accepting new connections and closes existing ones.
     * @returns {Promise<void>} A promise that resolves when the server is closed.
     */
    close(): Promise<void> {
        return this._server.close();
    }
}


