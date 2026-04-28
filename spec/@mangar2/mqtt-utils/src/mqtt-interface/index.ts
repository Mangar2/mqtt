/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

'use strict'

import { Types } from '@mangar2/utils'
import { topics_t, headers_t } from './interfaces';
import { publish, onPublish, IPublishOptions } from  './publish';
import { pubrel, onPubrel } from './pubrel';
import { subscribe, onSubscribe, SubscribeOptions, SubscribeResult } from  './subscribe';
import { unsubscribe, onUnsubscribe, UnsubscribeOptions, UnsubscribeResult } from './unsubscribe';

import { connect, onConnect, ConnectOptions, ConnectResult } from './connect';

import { disconnect, onDisconnect } from './disconnect';
export { ConnectResult, SubscribeResult, UnsubscribeResult, ConnectOptions, SubscribeOptions, UnsubscribeOptions }

/**
 * @private
 * @description Extracts the version from a headers object
 * @param {Object} headers object with member version
 * @returns {string} version string
 */
function getVersion (headers: headers_t) {
    const version = Types.isString(headers.version) ?  headers.version : '0.0'
    if (onPublish[version] === undefined) {
        throw 'undefined version ' + version
    }
    return version
}

export const Interfaces = {
    /**
     * @description Creates the objects to publish to a client
     * @param {string} version interface version ('0.0' or '1.0')
     * @param {object} options publish options
     * @param {string} options.token connection token
     * @param {Object} options.message payload
     * @param {number} options.qos quality of service (0,1,2)
     * @param {number} options.dup duplicate flag 1 (true) or 0 (false). Identifies duplicate packages
     * @param {number} options.retain flag 1 (true) or 0 (false). Requrests to retain the message
     * @param {number} options.packetid unique id of the package
     * @returns {Object} {headers, payload, resultCheck(result)}
     */
    publish: (version: string, options: IPublishOptions) => {
        return publish[version](options)
    },

    /**
     * @description creates the return types for a receive message
     * @param {Object} headers input headers
     * @returns {Object} {headers, payload, statusCode, packetid}
     */
    onPublish: (headers: headers_t) => {
        return onPublish[getVersion(headers)](headers)
    },

    /**
     * @description creates the return types for a receive pubrel message
     * @param {string} version interface version ('0.0' or '1.0')
     * @param {string} token connection token
     * @param {number} packetid of the packet (must be the same id as send by publish!)
     * @returns {Object} {headers, payload, resultCheck(result)}
     */
    pubrel: (version: string, token: string, packetid: string) => {
        return pubrel[version]({ token, packetid })
    },
    /**
     * @description Creates the objects for a qos=2 commit message "pubcomp"
     * @param {Object} headers pubrel message headers
     * @returns {Object} {headers, payload, statusCode, packetid}
     */
    onPubrel: (headers: headers_t) => {
        return onPubrel[getVersion(headers)](headers)
    },
    /**
     * @description Subscribes to a client
     * @param {string} version interface version ('0.0' or '1.0')
     * @param {Object} topics {topic: qos, ...}
     * @param {string} clientId unique client identifier
     * @param {number|undefined} packetid unique id of the package (not used on version 0.0)
     * @returns {Object} {headers, payload, resultCheck(result)}
     */
    subscribe: (version: string, topics: topics_t, clientId: string, packetid: number) => {
        return subscribe[version](topics, clientId, packetid)
    },
    /**
     * @description Creates the subscribe result objects
     * @param {Object} headers input headers
     * @param {Array} result quality of service reply array
     * @returns {Object} {headers, payload, statusCode, packetid}
     */
    onSubscribe: (headers: headers_t, result: SubscribeResult) => {
        return onSubscribe[getVersion(headers)](headers, result)
    },
    /**
     * @description Creats the objects to unsubscribe from a client
     * @param {string} version interface version ('0.0' or '1.0')
     * @param {Array} topics array of topic strings
     * @param {string} clientId unique client identifier
     * @param {number|undefined} packetid unique id of the package (not used on version 0.0)
     * @returns {Object} {headers, payload, resultCheck(result)}
     */
    unsubscribe: (version: string, topics: topics_t, clientId: string, packetid: number) => {
        return unsubscribe[version](topics, clientId, packetid)
    },
    /**
     * @description creates the return types for a disconnect request
     * @param {Object} headers message headers
     * @returns {Object} {headers, payload, statusCode, packetid}
     */
    /**
     * @description creates the return types for an unsubscribe request
     * @param {Object} headers message headers
     * @param {Array} result array of unsubscribe results
     * @returns {Object} {headers, payload, statusCode, packetid}
     */
    onUnsubscribe: (headers: headers_t, result: UnsubscribeResult) => {
        return onUnsubscribe[getVersion(headers)](headers, result)
    },
    /**
     * @description Creates the objects to connect ot a broker
     * @param {string} version interface version '1.0' or '0.0'
     * @param {Object} options connect options
     * @returns {Object} {headers, payload, resultCheck(result)}
     */
    connect: (version: string, options: ConnectOptions) => {
        return connect[version](options)
    },
    /**

    * @description Creates the return types for a connect request
     * @param {Object} headers message headers
     * @param {Object} payload on connect payload to return
     * @returns {Object} {headers, payload, statusCode}
     */
    onConnect: (headers: headers_t, payload: ConnectResult) => {
        return onConnect[getVersion(headers)](payload)
    },
    /**
     * @description Creates the objects to disconnect from a broker
     * @param {string} clientId unique client identifier
     * @returns {Object} {headers, payload, resultCheck(result)}
     */
    disconnect: (version: string, clientId: string) => {
        return disconnect[version](clientId)
    },
    /**
     * @description creates the return types for a disconnect request
     * @param {Object} headers message headers
     * @returns {Object} {headers, payload, statusCode}
     */
    onDisconnect: (headers: headers_t) => {
        return onDisconnect[getVersion(headers)]()
    }
}
