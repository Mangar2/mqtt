
/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { Types, errorLog, readConfiguration } from '@mangar2/utils';
import { sanitize } from '@mangar2/mqtt-utils';
import CheckInput from '@mangar2/checkinput';
import { configurationSchema, defaultConfiguration } from './configuration-types'

/**
 * Retrieves the configuration from the specified file.
 * @param filename - The path to the configuration file.
 * @returns The configuration object.
 */
const getConfiguration = (filename: string) => {
    const checkConfiguration = new CheckInput(configurationSchema);
    // path to your configuration file
    const config = readConfiguration(filename)
    if (!Types.isObject(config.runservices)) {
        errorLog('The active configuration does not contain a "runservice" section, program stopped')
        process.exit(1)
    }
    config.runservices = sanitize(config.runservices, defaultConfiguration, checkConfiguration)
    return config
}

/**
 * Loads and sanitizes the configuration file, using "yahaconfig.json" as default name, if the process
 * parameters do not include a configuration file name 
 * @returns {Object} configuration object
 */
export const loadConfiguration = () => {
    const config = getConfiguration('yahaconfig.json')
    return config
}


