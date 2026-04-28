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
import { IResult, RequestDataV2, standardHeaderText, headers_t } from "./interfaces"

/**
 * Interface representing the options for the pubrel function.
 * @interface
 */
interface IPubrelOptions {
    token: string;
    packetid?: string;
}

/**
 * Function type for creating request data for pubrel operations.
 * @param {IPubrelOptions} options The options for pubrel operation.
 * @returns {RequestData} The request data for the pubrel operation.
 */
type PubrelFunction = (options: IPubrelOptions) => RequestDataV2;

/**
 * Creates the objects for a qos=2 commit message "pubcomp".
 */
export const pubrel: Record<string, PubrelFunction> = {
    '0.0': ({ token, packetid }): RequestDataV2 => {
        /**
         * Creates the request data for pubrel operation for protocol version 0.0.
         * @param {IPubrelOptions} options The options for pubrel operation.
         * @returns {RequestData} The request data for the pubrel operation.
         */
        const payload = { token };
        const headers: Record<string, string> = { ...standardHeaderText, version: '0.0' };
        
        if (Types.isNumber(packetid) || Types.isString(packetid)) {
            headers.id = packetid.toString();
        }

        const resultCheck = (result: IResult) => {

            if (result.statusCode !== 200) {
                throw new Error(`status code 200 expected, got ${result.statusCode}`)
            };
            if (!Types.isString(result.headers['content-type']) || !result.headers['content-type'].startsWith('text/plain')) {
                throw new Error(`content-type is not text/plain`);
            } 
            if (result.payload.toLowerCase() !== 'pubcomp') {
                throw new Error(`acknowledge 'pubcomp' expected, got ${result.payload}` );
            }
            if (result.headers.id !== packetid?.toString()) {
                throw new Error( `packet id ${packetid} expected, got ${result.headers.id}`);
            }
        }

        return { headers, payload, resultCheck };
    },

    '1.0': ({ token, packetid }): RequestDataV2 => {
        /**
         * Creates the request data for pubrel operation for protocol version 1.0.
         * @param {IPubrelOptions} options The options for pubrel operation.
         * @returns {RequestDataV2} The request data for the pubrel operation.
         */
        const payload = { token };
        const headers: Record<string, string> = { ...standardHeaderText, version: '1.0' };
        if (Types.isNumber(packetid) || Types.isString(packetid)) {
            headers.packetid = packetid.toString();
        }

        const resultCheck = (result: IResult) => {
            
            if (result.statusCode !== 204) {
                throw new Error(`status code 204 expected, got ${result.statusCode}`)
            };
            if (result.headers.packetid !== packetid?.toString()) {
                throw new Error( `packet id ${packetid} expected, got ${result.headers.packetid}`);
            }
            const packet = result.headers.packet;
            if (packet !== 'pubcomp') {
                throw new Error(`acknowledge 'pubcomp' expected, got ${packet}` );
            }

        }
        return { headers, payload, resultCheck };
    }
}

/**
 * Function type for creating response data for received pubrel messages.
 * @param {headers_t} headers The headers from the received pubrel message.
 * @returns {IResult} The response data for the pubrel message.
 */
type PurelResponseFunction = (headers: headers_t) => IResult;

/**
 * Creates the return types for a received pubrel message.
 */
export const onPubrel: Record<string, PurelResponseFunction> = {
    '0.0': (headers: headers_t): IResult => {
        /**
         * Creates response data for received pubrel message for protocol version 0.0.
         * @param {headers_t} headers The headers from the received pubrel message.
         * @returns {IResult} The response data for the pubrel message.
         */
        const version = '0.0';
        const packetid = headers.id;

        const result: IResult = {
            headers: { 'content-type': 'text/plain; charset=UTF-8', version, id: packetid },
            payload: 'pubcomp',
            statusCode: 200,
            packetid: Number(packetid)
        };

        return result;
    },
    '1.0': (headers: headers_t): IResult => {
        /**
         * Creates response data for received pubrel message for protocol version 1.0.
         * @param {headers_t} headers The headers from the received pubrel message.
         * @returns {IResultWithPacketid} The response data for the pubrel message.
         */
        const version = '1.0';
        const packet = 'pubcomp';
        const packetid = headers.packetid;

        const result: IResult = {
            headers: { 'content-type': 'application/json; charset=UTF-8', version, packet, packetid },
            payload: '',
            statusCode: 204,
            packetid: Number(packetid)
        };
        
        return result;
    }
}


