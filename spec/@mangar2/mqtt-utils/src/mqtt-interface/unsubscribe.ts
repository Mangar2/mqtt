/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

'use strict'

import { Types } from "@mangar2/utils";
import { IResult, RequestDataV2, standardHeaderJSON, standardHeaderText, topics_t, headers_t } from "./interfaces"

/**
 * Options to unsubscribe to a service
 */
export interface UnsubscribeOptions {
    clientId: string,
    topics?: topics_t,
}

/**
 * The result of an unsubscribe call
 */
export type UnsubscribeReturnCodes = 0x00 | 0x11;
export type UnsubscribeResult = UnsubscribeReturnCodes [];

/**
 * @private
 * @description Generates request data for a client unsubscribe operation.
 * @param topics - Array of topic strings
 * @param clientId - Unique client identifier
 * @param packetid - Unique id of the package. Not used for version '0.0'.
 * @returns Request data with headers, payload and a result check function.
 */
const unsubscribe: Record<string, (topics: topics_t, clientId: string, packetid: number) => RequestDataV2> = {
    '0.0': (topics, clientId) => {
        const payload = { topics, clientId };
        const headers = { ...standardHeaderText, version: '0.0' };

        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 200) {
                throw new Error(`status code 200 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('text/plain')) {
                throw new Error(`content-type is not text/plain`);
            } 
            if (result.payload.toLowerCase() !== 'unsuback') {
                throw new Error(`acknowledge 'unsuback' expected, got ${result.payload}` );
            }
        }

        return { headers, payload, resultCheck };
    },

    '1.0': (topics, clientId, packetid): RequestDataV2 => {
        const payload = { topics, clientId };
        const headers = { ...standardHeaderJSON, packetid: packetid.toString(), version: '1.0' };

        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 200 && result.statusCode !== 204) {
                throw new Error(`status code 200 or 204 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('application/json')) {
                throw new Error(`content-type is not application/json`);
            } 
            if (result.headers.packet !== 'unsuback') {
                throw new Error(`acknowledge 'unsuback' expected, got ${result.headers.packet}` );
            }
            if (Number(result.headers.packetid) !== packetid) {
                throw new Error(`wrong packet id expected: ${packetid}, got ${result.headers.packetid}`)
            }
            if (!Types.isString(result.payload)) {
                throw new Error('payload not provided');
            }
            // Backward compatibility, empty payload supported
            if (result.statusCode === 204 && result.payload === "") {
                return;
            }
            const payload: UnsubscribeResult = JSON.parse(result.payload);
            if (!Types.isArray(payload)) {
                throw new Error('payload has illegal format, array expected');
            }
            if (!payload.every(code => [0, 0x11].includes(code))) {
                throw new Error(`illegal return codes in payload`);
            }
        }

        return { headers, payload, resultCheck };
    }
}

/**
 * @private
 * @description Generates response data for an unsubscribe request.
 * @param headers - Input headers
 * @returns Response data with headers, payload, status code and packetid.
 */
const onUnsubscribe: Record<string, (headers: headers_t, result: UnsubscribeResult) => IResult> = {
    '0.0': headers => ({
        headers: { 'content-type': 'text/plain; charset=UTF-8', version: '0.0' },
        payload: 'unsuback',
        statusCode: 200,
        packetid: Number(headers.id)
    }),

    '1.0': (headers, result) => {
        const packetid = headers.packetid;
        return {
            headers: { 'content-type': 'application/json; charset=UTF-8', packet: 'unsuback', version: '1.0', packetid },
            payload: JSON.stringify(result),
            statusCode: 200,
            packetid: Number(packetid)
        }
    }
}

export { unsubscribe, onUnsubscribe };
