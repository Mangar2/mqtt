/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Filters logs by a topic string and prints them to console
 */

import  CheckInput from '@mangar2/checkinput';
import { IMessage, Message, qos_t, TopicMatch } from './index';  

export type LogPattern = {
    topic: string;
    module: string;
    level?: number;
}

export type direction_t = 'in' | 'out' | 'all';

type LogOptions = {
    direction: direction_t;
    serviceName?: string;
    message: IMessage;
    dup?: boolean;
    logQos?: qos_t;
}

/**
 * @class LogFilter
 * @description
 * Provides a simple filter for logging topic based messages.
 * @example
 * const logfilter = new LogFilter();
 * logfilter.changePattern([{ topic: 'input/%', module: 'receive', level: 1 }]);
 * logfilter.condLogMessage('receive', new Message('hello world', 1), 1);
 */
export class Logger {
    private static _logger: Logger;
    private moduleFilter: Record<string, TopicMatch> = {};
    private checkTopicList = new CheckInput({
        type: 'array',
        items: {
            type: 'object',
            properties: {
                topic: { type: 'string' },
                module: { type: 'string' },
                level: { type: 'number', minimum: 0 }
            },
            required: ['topic', 'module']
        }
    });
    private _messages: IMessage[] = [];
    private _clientName: string = '';

    /**
     * Gets the messages that have been logged.
     */
    get messages(): IMessage[] {
        return this._messages;
    }

    set clientName(clientName: string) {
        this._clientName = clientName;
    }

    get clientName(): string {
        return this._clientName;
    }

    /**
     * Gets the logger instance.
     */
    static get logger(): Logger {
        if (!Logger._logger) {
            Logger._logger = new Logger();
        }
        return Logger._logger;
    }   

    /**
     * Replaces the log filter patterns, deleting the current patterns and replace them with the new patterns.
     * @param {LogPattern[]} patternList
     * @throws {Error} If the parameter is badly formatted.
     */
    changePattern(patternList: LogPattern[]): void {
        this.moduleFilter = {};
        this.checkTopicList.throwOnValidationError(patternList, 'log filter error');
        for (const pattern in patternList) {
            const filter = patternList[pattern];
            const module = filter.module;
            const level = filter.level !== undefined ? filter.level : 0;
            if (this.moduleFilter[module] === undefined) {
                this.moduleFilter[module] = new TopicMatch();
            }
            this.moduleFilter[module].addPattern(filter.topic, level);
        }
    }

    /**
     * Gets the log level.
     * @param {string} module name of the module to check for.
     * @param {string} topic topic to check.
     * @returns {undefined | number} returns a log level or undefine, if logging is false.
     */
    getLogLevel(module: string, topic: string): number | undefined {
        let result: number | undefined;
        if (typeof this.moduleFilter[module] === 'object') {
            result = this.moduleFilter[module].getBestMatch(topic);
        }
        if (typeof this.moduleFilter.all === 'object') {
            const match = this.moduleFilter.all.getBestMatch(topic);
            if (result === undefined || match > result) {
                result = match;
            }
        }
        return result;
    }

    /**
     * Logs a message with the specified details.
     * @param direction - The name of the service or module.
     * @param topic - The topic of the message.
     * @param text - The text of the message.
     * @param qos - The quality of service. (optional)
     */
    log(direction: direction_t, topic: string, text: string, qos: qos_t = 0, _serviceName = '') {
        const logLevel = this.getLogLevel(direction, topic);
        
        if (logLevel) {
            const logText = `${new Date().toLocaleString()} ${this._clientName} ${text}`
            const message = new Message(`$SYS/${this._clientName}/log`, logText, 'log', qos, false);
            this._messages.push(message);
        }
    }


    /**
     * Logs a message with the specified options.
     * @param options - The options for the message.
     * @param options.direction - The direction of the message (e.g., 'received' or 'sent').
     * @param options.message - The message to log.
     * @param options.serviceName - The name of the service.
     * @param options.dup - Whether the message is a duplicate.
     * @param options.logQos - The QoS level to log.
     */
    logMessage({ direction, message, serviceName = '', dup = false, logQos = 0 }: LogOptions): void {
        const { topic, value, reason, qos, retain } = message;

        const reasonStr = Array.isArray(reason)
            ? (direction === 'in'
                ? reason[0].message
                : reason[reason.length - 1].message)
            : '';
        const directionString = direction === 'in' ? 'received' : 'sent';            
        const valueStr = value ? `=${value}` : '';
        const dupStr = dup ? ',dup' : '';
        const retainStr = retain ? ',retain' : '';
        const serviceStr = serviceName ? `${serviceName} ` : '';
        const logText = ` (qos${qos}${dupStr}${retainStr}) ${directionString} ${serviceStr} ${topic}${valueStr} [${reasonStr}]`;
        this.log(direction, topic, logText, logQos, serviceName);
    }

    /**
     * Prints the log to the console.
     */
    printLog() {
        for (const message of this._messages) {
            console.log(message.value);
        }
    }   

    /**
     * Clears the messages array.
     */
    clearLog() {
        this._messages = [];
    }   
}
