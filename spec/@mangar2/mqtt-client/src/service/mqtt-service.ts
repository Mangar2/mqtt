/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Helping functions to send mqtt data
 */

const DEBUG = false;

import { errorLog } from '@mangar2/utils';
import { IncomingHttpHeaders } from '@mangar2/httpservice';
import { HttpOnConnect } from '@mangar2/mqtt-client/src/http-services/http-on-connect'
import { OnPublish } from '@mangar2/mqtt-client/src/service/on-publish';
import { IMqttServer } from '@mangar2/mqtt-client/src/service/imqtt-server';
import { IResult } from '@mangar2/mqtt-utils';

export class MqttService { 
    public onConnect: HttpOnConnect;
    public onPublish: OnPublish;

    constructor(mqttServer: IMqttServer) {
        this.onConnect = new HttpOnConnect(mqttServer);
        this.onPublish = new OnPublish();
    }

    /**
     * @description
     * Calls a connection interface
     * @param {string} control name of the mqtt controls to call (connect, disconnect, ...)
     * @param {Object} payload parameters for the interface (see interfaces)
     * @param {Object} headers headers of the http message
     * @returns {httpReturn} http return information
     */
    processRequest (control: string, payload: any, headers:IncomingHttpHeaders): IResult {
        let result: IResult = { headers: { 'Content-Type': 'text/plain' }, payload: 'request path not found: ' + control, statusCode: 404 }

        try {
            if (control in ['/connect']) {
                this.onConnect.handleHttpRequest(control, headers, payload);
            }
        } catch (err) {
            let error = 'Unknown error'
            if (err instanceof Error) {
                error = control + ' ' + err.message
                errorLog(err, DEBUG);
            } else if (typeof (err) === 'string') {
                error = control + ' ' + err
                errorLog(err, DEBUG);
            }
            // this.publishLogMessage(controls, 'fail', 'client request', payload.clientId)
            result = {
                headers: { 'Content-Type': 'application/json' },
                payload: JSON.stringify({ error }),
                statusCode: 400
            }
        }
        return result;
    }
}
