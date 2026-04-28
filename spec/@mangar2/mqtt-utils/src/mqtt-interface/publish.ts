/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { IMessage } from "../message";
import { Types } from "@mangar2/utils";
import { IResult, RequestDataV2, standardHeaderJSON, headers_t } from "./interfaces"
import  CheckInput from '@mangar2/checkinput'

/**
 * @private
 * @description
 * Checks the structure of a subscribe and unsubscribe
 */
export const checkPublishPayload = new CheckInput({
    type: 'object',
    properties: {
        clientId: { type: 'string' },
        topics: { type: 'array', items: { type: 'string' } }
    },
    required: ['clientId']
})

/**
 * Represents the options for publishing a message.
 * @interface
 */
export interface IPublishOptions {
    token: string;
    message: IMessage;
    dup?: boolean;
    packetid?: number;
}

/**
 * Defines the signature for a publish function.
 * @param {IPublishOptions} options The publishing options.
 * @returns {RequestData} The request data for publishing.
 */
interface PublishFunction {
    (options: IPublishOptions): RequestDataV2;
}

/**
 * Maps publish functions by their version numbers.
 */
export const publish: Record<string, PublishFunction> = {
    '0.0': (options: IPublishOptions) => {
        /**
         * Publish function for protocol version 0.0.
         * @param {IPublishOptions} options The publishing options.
         * @returns {RequestData} The request data for publishing.
         */
        const { message, packetid } = options;
        const { qos = 1 } = message;
        const payload = message;
        const headers: Record<string, string> = { ...standardHeaderJSON, qos: qos.toString(), version: '0.0' };
        if (Types.isNumber(packetid) || Types.isString(packetid)) {
            headers.id = packetid.toString();
        }

        /**
         * Checks the result of the publish operation.
         * @param {IResult} result The result to be checked.
         * @throws {Error} error message, if the result is not ok
         * @returns {boolean} True if the publish operation is successful, false otherwise.
         */
        const resultCheck = (result: IResult) => {
            if (qos === 0) return;

            if (result.statusCode !== 200) {
                throw new Error(`status code 200 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('text/plain')) {
                throw new Error('content-type is not text/plain');
            } 
            if (result.payload.toLowerCase() !== 'puback') {
                throw new Error(`acknowledge 'puback' expected, got ${result.payload}` );
            }
            if (result.headers.id !== packetid?.toString()) {
                throw new Error( `packet id ${packetid} expected, got ${result.headers.id}`);
            }
        }

        return { headers, payload, resultCheck };
    },

    '1.0': (options: IPublishOptions) => {
        /**
         * Publish function for protocol version 1.0.
         * @param {IPublishOptions} options The publishing options.
         * @returns {RequestData} The request data for publishing.
         */
        const {token, message, dup = 0, packetid } = options;
        const {  qos = 1, retain = false } = message;
        const payload = { token, message: { topic: message.topic, value: message.value, reason: message.reason } };
        const headers: Record<string, string> = { 
            ...standardHeaderJSON, 
            qos: qos.toString(), 
            dup: dup ? '1' : '0',
            retain: retain ? '1': '0', 
            version: '1.0' 
        };
        if (Types.isNumber(packetid) || Types.isString(packetid)) {
            headers.packetid = packetid.toString();
        }

        /**
         * Checks the result of the publish operation.
         * @param {IResult} result The result to be checked.
         * @throws {Error} error message, if the result is not ok
         * @returns {{success: boolean, message?:string}} success true on success, else false with error message
         */
        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 204) {
                throw new Error(`status code 204 expected, got ${result.statusCode}`)
            };
            if (result.headers.packetid !== packetid?.toString()) {
                throw new Error( `packet id ${packetid} expected, got ${result.headers.packetid}`);
            }
            const packet = result.headers.packet;
            if (qos === 1 && packet !== 'puback') {
                throw new Error(`acknowledge 'puback' expected, got ${packet}` );
            }
            if (qos === 2 && packet !== 'pubrec') {
                throw new Error(`acknowledge 'pubrec' expected, got ${packet}` );
            }
        }

        return { headers, payload, resultCheck };
    }
};

/**
 * Defines the signature for a publish response function.
 * @param {headers_t} headers The headers received from the publish request.
 * @returns {IResult} The result of the publish response.
 */
interface PublishResponseFunction {
    (headers: headers_t): IResult;
}

/**
 * Maps publish response functions by their version numbers.
 */
export const onPublish: Record<string, PublishResponseFunction> = {
    '0.0': (headers: headers_t): IResult => {
        /**
         * Publish response function for protocol version 0.0.
         * @param {headers_t} headers The headers received from the publish request.
         * @returns {IResult} The result of the publish response.
         */
        const version = '0.0';
        const packetid = headers.id;
        
        const result: IResult = {
            headers: { 'content-type': 'text/plain; charset=UTF-8', version },
            payload: 'puback',
            statusCode: 200,
        }

        if (Types.isNumber(packetid) || Types.isString(packetid)) {
            result.headers.id = packetid.toString();
            result.packetid = Number(packetid);
        }
        return result;
    },

    '1.0': (headers: headers_t): IResult => {
        /**
         * Publish response function for protocol version 1.0.
         * @param {headers_t} headers The headers received from the publish request.
         * @returns {IResult} The result of the publish response.
         */
        const version = '1.0';
        const qos = Number(headers.qos);
        const packetid = headers.packetid;

        const result: IResult = {
            headers: { 'content-type': 'application/json; charset=UTF-8', version, qos: headers.qos, retain: headers.retain },
            payload: '',
            statusCode: 204
        }

        if (Types.isNumber(packetid) || Types.isString(packetid)) {
            result.headers.packetid = packetid.toString();
            result.packetid = Number(packetid);
        }

        if (qos === 1) { result.headers.packet = 'puback'; }
        if (qos === 2) { result.headers.packet = 'pubrec'; }

        return result;
    }
}
