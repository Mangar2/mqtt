/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Creates Mqtt message handler, receives, checks, transforms and foreward messages
 * It supports the callbacks 'send' and 'publish'
 */

import  CheckInput from '@mangar2/checkinput'

/**
 * @private
 * @description
 * Checks the payload structure of a connect command
 */
export const checkConnectPayload = new CheckInput({
    type: 'object',
    properties: {
        clientId: { type: 'string' },
        host: { type: 'string' },
        port: { type: ['integer'] },
        clean: { enum: ['true', 'false', true, false] },
        keepAlive: { type: ['number', 'string'] },
        password: { type: 'string' },
        user: { type: 'string' },
        will: {
            type: 'object',
            properties: ''
        }
    },
    required: ['clientId', 'host', 'port', 'clean']
})

/**
 * @private
 * @description
 * Checks the payload structure of a disconnect command
 */
export const checkDisconnectPayload = new CheckInput({
    type: 'object',
    properties: {
        clientId: { type: 'string' }
    },
    required: ['clientId']
})

/**
 * @private
 * @description
 * Checks the structure of a subscribe and unsubscribe
 */
export const checkSubscribePayload = new CheckInput({
    type: 'object',
    properties: {
        clientId: { type: 'string' },
        topics: { type: 'object' },
        subscribe: {
            type: 'object',
            properties: {
                QoS: {
                    enum: ['0', '1', '2', 0, 1, 2]
                },
                topics: {
                    type: ['string', 'array'],
                    items: { type: 'string' }
                }
            }
        }
    },
    required: ['clientId']
})

/**
 * @private
 * @description
 * Checks the structure of a subscribe and unsubscribe
 */
export const checkUnsubscribePayload = new CheckInput({
    type: 'object',
    properties: {
        clientId: { type: 'string' },
        topics: { type: 'array', items: { type: 'string' } }
    },
    required: ['clientId']
})

