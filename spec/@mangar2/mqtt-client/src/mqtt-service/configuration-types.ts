/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2024 Volker Böhm
 */


type LogEntry = {
    module: 'all' | 'send' | 'receive';
    topic: '#'; 
    level: number;
}

interface Broker {
    port: string | number;
    host: string;
}

export interface RunServices {
    clientId: string;
    version: '0.0' | '1.0';
    clean: boolean;
    broker: Broker;
    listener: string | number;
    keepAliveInSeconds: number;
    services: string[];
    log: LogEntry[];
}

export interface IServiceConfiguration {
    intervalInSeconds: number;
    [key: string]: any;
}

export interface ServiceContainer {
    runservices: RunServices;
    [key: string]: IServiceConfiguration | RunServices;
}

export const configurationSchema = {
    properties: {
        clientId: { type: 'string' },
        version: { enum: ['0.0', '1.0'] },
        clean: { type: 'boolean' },
        broker: { $ref: '#broker' },
        listener: { type: ['string', 'number'] },
        keepAliveInSeconds: { type: 'number' },
        log: { $ref: '#log' },
        services: {
            type: 'array',
            items: {
                enum: [
                    'automation',
                    'messageStore',
                    'opensensemap',
                    'pushover',
                    'remoteService',
                    'rs485Interface',
                    'sunnyportal',
                    'serialDevice',
                    'valueService',
                    'zwave',
                    'test-service'
                ]
            }
        },
        required: ['clientId', 'version', 'clean', 'broker', 'listener', 'keepAliveInSeconds', 'services'],
        additionalProperties: false
    },
    definitions: {
        broker: {
            $id: '#broker',
            type: 'object',
            properties: {
                port: { type: ['string', 'number'] },
                host: { type: 'string' }
            },
            required: ['port', 'host'],
            additionalProperties: false
        },
        log: {
            $id: '#log',
            type: 'array',
            items: {
                type: 'object',
                properties: {
                    module: { enum: ['all', 'send', 'receive'] },
                    topic: { type: '#' },
                    level: { type: 'integer' }
                },
                required: ['module', 'topic'],
                additionalProperties: false
            }
        }
    }
}

export const defaultConfiguration = {
    clientId: 'yaha/internal',
    version: '1.0',
    clean: true,
    broker: {
        port: 8183,
        host: '127.0.0.1'
    },
    listener: 8204,
    keepAliveInSeconds: 600,
    log: [
        {
            module: 'send',
            topic: '#',
            level: 1
        },
        {
            module: 'all',
            topic: '$SYS/#',
            level: 0
        }
    ]
}
