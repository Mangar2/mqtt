/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { Types } from "@mangar2/utils";
import { IResult, RequestDataV2, standardHeaderJSON } from "./interfaces";
import { IMessage, qos_t } from '../message';

/**
 * The result of an mqtt connect call
 */
export type ConnectResult = {
    mqttcode?: 0 | 1 | 2 | 3 | 4 | 5;
    present: number;
    token: { send: string, receive: string };
}

/**
 * Type representing the connect payload provided by the client.
 */
export type ConnectOptions = {
    qos?: qos_t;
    clientId?: string;
    version?: string;
    host?: string;
    port?: number;
    clean: boolean;
    keepAlive?: number;
    password?: string;
    user?: string;
    will?: IMessage;
}


/**
 * Connection function interface.
 */
interface ConnectionFunction {
    (options: ConnectOptions): RequestDataV2
}

/**
 * Connect functions mapped by their version numbers
 * @private
 */
export const connect: Record<string, ConnectionFunction> = {
    '0.0': (options: ConnectOptions) => {
        const { clientId, host, port, clean } = options;
        const payload = {
            clientId, host, port, clean
        };
        const headers = { ...standardHeaderJSON, version: '0.0' };

        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 200) {
                throw new Error(`status code 200 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('text/plain')) {
                throw new Error(`content-type is not text/plain`);
            } 
            if (result.payload.toLowerCase() !== 'connack') {
                throw new Error(`acknowledge 'connack' expected, got ${result.payload}` );
            }
        }

        return { headers, payload, resultCheck };
    },

    '1.0': (options: ConnectOptions) => {
        const payload = { ...options }
        payload.keepAlive = payload.keepAlive ?? 0;
        const headers = { ...standardHeaderJSON, version: '1.0' };

         /**
         * Checks the result of the publish operation.
         * @param {IResult} result The result to be checked.
         * @returns {{success: boolean, message?:string}} success true on success, else false with error message
         */
         const resultCheck = (result: IResult) => {
            if (result.statusCode !== 200) {
                throw new Error(`status code 200 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('application/json')) {
                throw new Error(`content-type is not application/json`);
            } 
            if (result.headers.packet !== 'connack') {
                throw new Error(`acknowledge 'connack' expected, got ${result.headers.packet}` );
            }
            if (!Types.isString(result.payload)) {
                throw new Error('payload not provided');
            }
            const payload: ConnectResult = JSON.parse(result.payload);
            if (!Types.isObject(payload)) {
                throw new Error('connect bad return type, object expected');
            }
            if (payload.present !== 0 && payload.present !== 1) {
                throw new Error(`connect illegal present flag value ${payload.present}`);
            }
            if (payload.mqttcode !== undefined && payload.mqttcode !== 0) {
                const codes = {
                    1: 'unacceptable protocol version',
                    2: 'identifier rejected',
                    3: 'server unavailable',
                    4: 'bad user name or password',
                    5: 'not authorized'
                }
                throw new Error(`mqtt connect error ${codes[payload.mqttcode]} mqttcode: ${payload.mqttcode}`);
            }
            if (!payload.token || !Types.isString(payload.token.send) || !Types.isString(payload.token.receive)) {
                throw new Error ('send/receive token not completely received')
            }
        }

        return { headers, payload, resultCheck };
    }
}

/**
 * Connection response interface.
 */
interface ConnectionResponseFunction {
    (payload: ConnectResult): IResult
}

/**
 * OnConnect functions mapped by their version numbers
 * @private
 */
export const onConnect: Record<string, ConnectionResponseFunction> = {
    '0.0': () => {
        return {
            headers: { 'content-type': 'text/plain; charset=UTF-8', version: '0.0' },
            payload: 'connack',
            statusCode: 200
        };
    },

    '1.0': (payload: ConnectResult) => {
        return {
            headers: { 'content-type': 'application/json; charset=UTF-8', packet: 'connack', version: '1.0' },
            payload: JSON.stringify(payload),
            statusCode: 200
        };
    }
}

