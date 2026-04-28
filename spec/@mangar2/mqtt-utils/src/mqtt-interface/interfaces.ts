/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { qos_t } from "../message";
import { IncomingHttpHeaders } from 'http';


/** Types for the 'topics' object */
export type topics_t = { [key: string]: qos_t };

export type headers_t = IncomingHttpHeaders;

/**
 * Types representing the result object for interfaces
 * 
 */
export interface IResult {
    statusCode: number;
    headers: headers_t;
    payload: string;
    present?: 0 | 1; // true (1), if the client is already present and not clean
    token?: string;
    runtime?: number;
    packetid?: number;
}

/**
 * Type representing the information returned for requests
 */
export interface RequestData {
    headers: Record<string, string>,
    payload: Record<string, any>,
    resultCheck: (result: IResult) => boolean
}

/**
 * Type representing the information returned for requests
 */
export interface RequestDataV2 {
    headers: Record<string, string>,
    payload: Record<string, any>,
    resultCheck: (result: IResult) => void
}

/**
 * Standard header components for application/json messages
 */
export const standardHeaderJSON: Record<string, string> = {
    'content-type': 'application/json; charset=UTF-8',
    accept: 'application/json,text/plain',
    'accept-charset': 'UTF-8'
};

/**
 * Standard header components for text/plain messages
 */
export const standardHeaderText: Record<string, string> = {
    'content-type': 'text/plain; charset=UTF-8',
    accept: 'application/json,text/plain',
    'accept-charset': 'UTF-8'
};

/**
 * Supported types for ids
 */
export type idType = string | number;