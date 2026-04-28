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

console.log('test firstMatch')
require('@mangar2/mqtt-utils/test/topic/bestmatch/match/test')

console.log('test bestMatch')
require('@mangar2/mqtt-utils/test/topic/bestmatch/bestmatch/test')

console.log('delete')
require('@mangar2/mqtt-utils/test/topic/bestmatch/delete/test')

console.log('performancetest (< 1 second)')
require('@mangar2/mqtt-utils/test/topic/bestmatch/perftest/test')
