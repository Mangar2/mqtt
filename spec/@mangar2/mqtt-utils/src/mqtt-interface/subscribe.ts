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
import { IResult, RequestDataV2, standardHeaderJSON, topics_t, headers_t } from "./interfaces"

/**
 * Options to subscribe to a service
 */
export interface SubscribeOptions {
    clientId: string,
    topics?: topics_t,
    subscribe?: { QoS: 0 | 1 | 2, topics: string | string[] }
}

/**
 * The result of a subscribe call
 */
export type SubscribeReturnCodes = 0 | 1 | 2 | 0x7F | 0x80;
export type SubscribeResult = { qos: SubscribeReturnCodes [] };

/** Function type for Subscribe request */
interface SubscribeRequestFunction {
    (topics: topics_t, clientId: string, packetid: number): RequestDataV2
}

/** Function type for Subscribe response */
interface SubscribeResponseFunction {
    (headers: headers_t, qosArray: SubscribeResult): IResult;
}



/** Object to define the functions for each protocol version for a subscribe request
 * @private
 * @description Subscribes to a client
 * @param {Object} topics {topic: qos, ...}
 * @param {string} clientId unique client identifier
 * @param {string} packetid unique id of the package (not used on version 0.0)
 * @returns {Object} {headers, payload, resultCheck(result)}
 */
export const subscribe: Record<string, SubscribeRequestFunction> = {
    '0.0': (topics: topics_t, clientId: string): RequestDataV2 => {
        const subscribeData = { QoS: 0, topics: [] as string[] }
        for (const topic in topics) {
            subscribeData.QoS = topics[topic];
            subscribeData.topics.push(topic)
        }
        const payload = {
            clientId, subscribe: subscribeData
        }
        const headers = { ...standardHeaderJSON, version: '0.0' }

        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 200) {
                throw new Error(`status code 200 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('text/plain')) {
                throw new Error(`content-type is not text/plain`);
            } 
            if (result.payload.toLowerCase() !== 'suback') {
                throw new Error(`acknowledge 'suback' expected, got ${result.payload}` );
            }
        }

        return { headers, payload, resultCheck }
    },

    '1.0': (topics: topics_t, clientId: string, packetid: number): RequestDataV2 => {
        const payload = {
            clientId, topics
        }
        const headers = { ...standardHeaderJSON, packetid: packetid.toString(), version: '1.0' }

        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 200) {
                throw new Error(`status code 200 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('application/json')) {
                throw new Error(`content-type is not application/json`);
            } 
            if (result.headers.packet !== 'suback') {
                throw new Error(`acknowledge 'suback' expected, got ${result.headers.packet}` );
            }
            if (Number(result.headers.packetid) !== packetid) {
                throw new Error(`wrong packet id expected: ${packetid}, got ${result.headers.packetid}`)
            }
            if (!Types.isString(result.payload)) {
                throw new Error('payload not provided');
            }
            const payload: any = JSON.parse(result.payload);
            if (!Types.isObject(payload) && !Types.isArray(payload.qos)) {
                throw new Error('payload has illegal format, it must be an object with "qos" as an array of numbers');
            }
            else if (!payload.qos.every((code: number) => [0, 1, 2, 0x7F, 0x80].includes(code))) {
                throw new Error(`illegal return codes in payload`);
            }
        }
        return { headers, payload, resultCheck }
    }
}

/** Object to define the functions for each protocol version for a subscribe response 
 * @description Creates the subscribe result objects
 * @param {Object} headers input headers
 * @param {Array} qosArray quality of service reply array
 * @returns {Object} {headers, payload, statusCode, packetid}
 */
export const onSubscribe: Record<string, SubscribeResponseFunction> = {
    '0.0': (headers: headers_t): IResult => {
        return {
            headers: { 'content-type': 'text/plain; charset=UTF-8', version: '0.0' },
            payload: 'suback',
            statusCode: 200,
            packetid: Number(headers.id)
        }
    },

    '1.0': (headers: headers_t, result: SubscribeResult): IResult => {
        const packetid = headers.packetid

        return {
            headers: { 'content-type': 'application/json; charset=UTF-8', version: '1.0', packet: 'suback', packetid },
            payload: JSON.stringify(result),
            statusCode: 200,
            packetid: Number(headers.packetid)
        }
    }
}



