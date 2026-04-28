/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview Interface to the opensense map service. Use it to send data to opensense map.

 */

'use strict'

const RemoteService = require('./remoteservice.js')

/**
 * Prepares the object
 * @param {Object} config remote service configuration
 * @param {RemoteService} [remoteService=null] existing remote service class
 * @returns {RemoteService} the prepared remote service object
 */
function prepare (config, remoteService) {
    if (!remoteService) {
        remoteService = new RemoteService(config)
    }

    return remoteService
}

module.exports = {
    RemoteService,
    prepare
}
