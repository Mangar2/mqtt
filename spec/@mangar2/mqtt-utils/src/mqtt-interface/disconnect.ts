/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { IResult, RequestDataV2, standardHeaderJSON, standardHeaderText } from "./interfaces"

/**
 * Disconnect function interface.
 */
interface DisconnectFunction {
    (clientId: string): RequestDataV2;
}

/**
 * Disconnect functions mapped by their version numbers
 * @private
 */
export const disconnect: Record<string, DisconnectFunction> = {
    '0.0': (clientId: string) => {
        const payload = { clientId };
        const headers = { ...standardHeaderText, version: '0.0' };

        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 204) {
                throw new Error(`status code 204 expected, got ${result.statusCode}`)
            };
        }

        return { headers, payload, resultCheck };
    },

    '1.0': (clientId: string) => {
        const payload = { clientId };
        const headers = { ...standardHeaderJSON, version: '1.0' };

        const resultCheck = (result: IResult) => {
            if (result.statusCode !== 204) {
                throw new Error(`status code 204 expected, got ${result.statusCode}`)
            };
        }

        return { headers, payload, resultCheck };
    }
};


/**
 * OnDisconnect functions mapped by their version numbers
 * @private
 */
export const onDisconnect: Record<string, () => IResult> = {
    '0.0': () => {
        return {
            headers: { 'content-type': 'text/plain; charset=UTF-8', version: '0.0' },
            payload: '',
            statusCode: 204
        }
    },

    '1.0': () => {
        return {
            headers: { 'content-type': 'application/json; charset=UTF-8', version: '1.0' },
            payload: '',
            statusCode: 204
        }
    }
}

