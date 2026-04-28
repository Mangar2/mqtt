/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import * as http from 'http';
import * as https from 'https';
import { Socket } from 'net';
import { Callbacks } from '@mangar2/utils';

type IncomingMessage = http.IncomingMessage 
type ServerResponse = http.ServerResponse


interface ServerOptions {
    // Define additional specific options here if needed
}

/**
 * Callback for Http(S) GET requests
 * @callback HttpGetCallback
 * @param {URLSearchParams} params Search parameter of the GET URL
 * @param {Object} headers headers
 * @param {string} path path name (without search parameters)
 * @param {Object} res http(s) res structure
 */
type HttpGetCallback = (params: URLSearchParams, headers: Record<string, string | string[] | undefined>, path: string, res: ServerResponse) => void;

/**
 * Callback for Http(S) put, post, patch, delete, listen, or closed requests
 * @callback HttpCallback
 * @param {string} payload http payload (maybe stringified JSON)
 * @param {Object} headers headers
 * @param {string} path path name (without search parameters)
 * @param {Object} res http(s) res structure
 */
type HttpCallback = (payload: string, headers: Record<string, string | string[] | undefined>, path: string, res: ServerResponse) => void;

/**
 * @private
 * @description
 * Creates a http/https server listening to a certain port for
 * simplifying access to the node http(s) service
 * Register your callbacks according to the http(s) functions you need.
 * @param {number} port port to listen to
 */
class Server {
    private port: number;
    private sockets: Record<number, Socket>;
    private nextSocketId: number;
    server: http.Server | https.Server | undefined;
    private callbacks: Callbacks;

    constructor(port: number) {
        this.port = port;
        this.sockets = {};
        this.nextSocketId = 0;
        this.server = undefined;
        this.callbacks = new Callbacks(['get', 'put', 'post', 'patch', 'delete', 'options', 'listen', 'closed']);

        this.on('listen', () => {
            const address = this.server?.address();
            const port = typeof address === 'string' ? null : address?.port;
            console.log('Server listening on: http://localhost:%s', port);
        });
        this.on('closed', () => {
        });
    }

    /**
     * Sets a callback
     * @param {string} event RESTful http verb (POST, GET, PUT, PATCH, DELETE) and 'listen' (called once listening) and 'closed' (called once closed)
     * @param {HttpGetCallback|HttpCallback} callback(payload, headers, path, res)
     * where payload is the http payload, headers the http headers, res the result structure and path is
     * is the http path as string
     */
    on(event: string, callback: HttpGetCallback | HttpCallback): void {
        this.callbacks.on(event.toLowerCase(), callback);
    }

    /**
     * @private
     * @description
     * Handles an error situation
     * @param {string|Error} err error message
     * @param {res} object http result structure
     */
    private onError(err: string | Error, res: ServerResponse): void {
        res.writeHead(400, { 'Content-Type': 'text/plain' });
        res.end(typeof err === 'object' ? err.message : err);
        console.error(err);
    }

    /**
     * dispatches a http request
     * @param {string} method name of the method
     * @param {Object} req request information
     * @param {Object} res http result structure
     * @param {Array} url parsed URI
     * @param {Object} body body data
     */
    private dispatch(method: string, payload: string, req: IncomingMessage, res: ServerResponse): void {
        if (!this.callbacks.hasCallback(method)) {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end(`unknown method ${method}`);
            return;
        }
        const url = new URL(req.url!, `http://${req.headers.host}`);
        const path = url.pathname;
        if (method === 'get') {
            this.callbacks.invokeCallback(method, url.searchParams, req.headers, path, res);
        } else {
            this.callbacks.invokeCallback(method, payload, req.headers, path, res);
        }
    }

    /**
     * Handles incoming Http requests and dispatches them.
     * @param {http.IncomingMessage} request The incoming Http request.
     * @param {http.ServerResponse} response The server response object.
     */
    private requestHandler(req: http.IncomingMessage, res: http.ServerResponse): void {
        try {
            const method = req.method?.toLowerCase() || '';
            let payload = '';

            req.on('data', (chunk: Buffer) => {
                payload += chunk.toString();
            });

            req.on('end', () => {
                try {
                    this.dispatch(method, payload, req, res);
                } catch (err) {
                    this.onError(err as string, res);
                }
            });
        } catch (err) {
            this.onError(err as string, res);
        }
    }

    /**
     * Creates a http server listening
     * @param {boolean} [useHttpS] true, if we use https
     * @param {Object} options additional options
     */    
    listen(useHttpS: boolean = false, options?: ServerOptions): void {

        if (!useHttpS) {
            this.server = http.createServer((req, res) => this.requestHandler(req, res)).listen(this.port, () => {
                this.callbacks.invokeCallback('listen');
            });
        } else {
            this.server = https.createServer(options as https.ServerOptions, (req, res) => this.requestHandler(req, res)).listen(this.port, () => {
                this.callbacks.invokeCallback('listen');
            });
        }

        this.server.on('connection', (socket: Socket) => {
            const socketId = this.nextSocketId++;
            this.sockets[socketId] = socket;

            socket.on('close', () => {
                delete this.sockets[socketId];
            });
        });
    }

    /**
     * Stops the server from accepting new connections and closes existing connections.
     * Calls the callback "close", once the server is closed.
     * @returns {promise} resolved, once the connection is closed
     */    
    close(): Promise<void> {
        return new Promise((resolve) => {
            if (this.server) {
                this.server.close(() => {
                    this.callbacks.invokeCallback('closed');
                    resolve();
                });

                Object.values(this.sockets).forEach(socket => socket.destroy());
            }
        });
    }
}

export default Server;

