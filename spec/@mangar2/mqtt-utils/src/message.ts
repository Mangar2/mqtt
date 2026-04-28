/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * This is the basic class holding MQTT message information
 * The yaha home automation is based on messages. Nothing happens without a message.
 * The message runs from creator to broker and then to the receiver.
 * On every step, a reason will added to the mesage including a timestamp to get a full trace.
 */

'use strict'

'use strict'

import { Types } from '@mangar2/utils';
import CheckInput from '@mangar2/checkinput';

export type reasonEntry_t = {
    timestamp: string;
    message: string;
}

export type reason_t = Array<reasonEntry_t>;

export type qos_t = 0 | 1 | 2;
export type retain_t = boolean;

export interface IMessage {
    topic: string;
    value: string | number;
    reason: reason_t | undefined;
    qos?: qos_t;
    retain?: retain_t;
    clone(): IMessage;
}

/**
 * @description
 * Creates a message object that can be sent over MQTT
 * @param {string} topic - Topic string to publish to
 * @param {string|number} [value=''] - Value to set topic to
 * @param {string|reason_t|undefined} [reason] - Explaining why the topic will be set to value
 * @param {0,1,2} [qos=1] - quality of service level
 * @param {boolean} [retain=false] - wether the message shall be retained or not
 * @param {Date} [now= new Date()] - Current time
 * @example
 * const message = new Message('this/is/a/topic', 'a value', 'a reason')
 */
export class Message implements IMessage {
    public reason: reason_t | undefined;

    constructor(
        public topic: string,
        public value: string | number = '',
        reason: string | reason_t | undefined = undefined,
        public qos: qos_t = 1,
        public retain: retain_t = false,
        now: Date = new Date()
    ) {
        if (Types.isArray(reason)) {
            this.reason = [...reason];
        } else if (Types.isString(reason)) {
            this.addReason(reason, now);
        } 
    }

    /**
     * Returns true if the value is "on" (1, 'on', 'true', true), false otherwise
     * @type {boolean}
     */
    isOn(): boolean {
        return (this.value === 'on' || this.value === 'ON' || this.value === 'true' || this.value === 1);
    }

    /**
     * Deep-Clones the message
     * @returns a clone of the Message
     */
    clone(): Message {
        return new Message(this.topic, this.value, this.reason, this.qos, this.retain);
    }

    /**
     * Adds a new reason object to `this.reason`
     * @param {string} reason - Explaining why the topic will be set to value
     * @param {Date|undefined} now - Current time. Defaults to the current date if undefined.
     */
    addReason(reason: string, now: Date = new Date()): void {
        if (!Types.isArray(this.reason)) {
            this.reason = [];
        }
        this.reason.push({
            message: reason,
            timestamp: now.toISOString()
        });
    }

    /**
     * Gets the latest date of the message by browsing the reasons
     * @returns {Date | undefined}
     */
    getDateOfNewestChange(): Date | undefined {
        let time: Date | undefined = undefined;
        if (Types.isArray(this.reason)) {
            for (const element of this.reason) {
                const timestamp = element.timestamp;
                if (timestamp !== undefined) {
                    time = new Date(timestamp);
                    break;
                }
            }
        }
        return time;
    }

    /**
     * Validates that the message object matches the defined schema
     * @param {Object} object - Object to validate or `this`
     * @throws {error} - Throws an exception if the validation fails
     */
    static validate(message: IMessage): void {
        const checkMessage = new CheckInput({
            type: 'object',
            properties: {
                topic: { type: 'string' },
                value: { type: ['string', 'number'] },
                reason: {
                    type: 'array',
                    items: {
                        type: 'object',
                        properties: {
                            message: { type: 'string' },
                            timestamp: { type: 'string' }
                        },
                        required: ['message']
                    }
                },
                qos: { type: 'integer', minimum: 0, maximum: 2 },
                retain: { type: 'boolean' }
            },
            required: ['topic', 'value']
        });

        if (checkMessage.validate(message) !== true) {
            throw Error(JSON.stringify(checkMessage.messages, null, 2));
        }
    }

    /**
     * Creates a new message object having an IMessage data structure
     * It first validates that the data structure is correct.
     * @throws {error} - Throws an exception if the validation fails
     */
    static createMessage(message: IMessage, qos?: qos_t | string | string[], retain?: boolean | string | string[]): Message {
        if (retain !== undefined) {
            retain = retain === true || retain === '1';
        } else {
            retain = message.retain;
        }
        if (qos !== undefined) {
            qos = qos === '1' || qos === 1 ? 1 : qos === '2' || qos === 2 ? 2 : 0;
        } else {
            qos = message.qos;
        }

        const messageInstance = new Message(message.topic, message.value, message.reason, qos, retain);
        Message.validate(messageInstance);
        return messageInstance;
    }

}

