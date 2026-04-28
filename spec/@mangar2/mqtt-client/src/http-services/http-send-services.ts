/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Provides functions to connect to the (http based) yaha mqtt broker
 */

import { HttpClient } from '@mangar2/httpservice';
import { IMessage } from '@mangar2/mqtt-utils';
import { Publish } from '../service/publish';
import { BrokerOptions } from '../service/types';
import { headers_t } from './http-receive-services';


/**
 * Provides functions to send data to the broker
 */
export class HttpSendServices {
    private _httpClient: HttpClient; 
    private _publish: Publish;

    /**
     * Creates a new http service
     * @param port the port on which the server listens
     */
    constructor(brokerOptions: BrokerOptions) {
        this._httpClient = new HttpClient(brokerOptions.host, brokerOptions.port, 'http');
        this._publish = new Publish();
        this._publish.on('send', (httpMethod, path, headers, payload) => {
            return this._httpClient.sendv2({ path, method: httpMethod, headers, payload });
        })
    }

    /**
     * Sends an HTTP request.
     * 
     * @param path - The path of the request.
     * @param method - The HTTP method of the request.
     * @param headers - The headers of the request.
     * @param payload - The payload of the request.
     * @returns A promise that resolves to the response of the request.
     */
    public async send(path: string, headers: headers_t, payload: any) {
        return this._httpClient.sendv2({ path, method: 'put', headers, payload });
    }

    /**
     * Publishes a message
     */
    public async publish(token: string, message: IMessage, serviceName: string, version = '1.0'): Promise<string[]> {
        return this._publish.publish(token, message, serviceName, version);
    }

}
