/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @brief starts a service providing a key value store 
 */

'use strict'

import { KeyValueStore } from './keyvaluestore.js'
import { readConfiguration } from '@mangar2/config'

const configuration = readConfiguration('yahakvstore.json')
const keyValueStore = new KeyValueStore(configuration)
keyValueStore.run()
