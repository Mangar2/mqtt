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

import { Types } from '@mangar2/utils';
import { TopicMatch } from './topicmatch';

interface ITopicMapConfiguration {
    [key: string]: string | ITopicMapConfiguration;
}

/**
 * @class TopicMap
 * @description
 * Creates a class to map topics from internal to external representation and vice versa if there
 * is a matching mapping rule in configuration. It keeps unchanged if there is no mapping rule.
 *
 * @example
 * const configuration =
 *   fs20: {
 *       'fs20/2111': 'ground/livingroom/zwave/switch/multimedia/set'
 *   },
 *   location: {
 *       'location/33/#': 'first/bedroom/main/#'
 * }
 * const topicMap = new topicMap(configuration)
 * const map = topicMap('location/33/a/b') // returns 'first/bedroom/main/a/b'
 * const map = topicMap('anything/not/mapped') // returns 'anything/not/mapped'
 */
export class TopicMap {
    private _internalToExternal: Record<string, string> = {};
    private _externalToInternal: Record<string, string> = {};

    constructor(configuration: ITopicMapConfiguration) {
        this._setMapsRec(configuration);
    }

    /**
     * @private
     * Get the part of the topic that matches the '#' wildchard.
     * @param {string[]} topicChunks elements of the topic
     * @param {string[]} patternChunks elements of the pattern
     */
    private _getTopicEnd(topicChunks: string[], patternChunks: string[]): string {
        let result = ''
        while (patternChunks.length > 0) {
            if (patternChunks[0] === '#') {
                result = topicChunks.join('/')
                break
            }
            if (patternChunks[0] !== topicChunks[0]) {
                throw Error('fatal matching error, should not happen')
            }
            patternChunks.shift()
            topicChunks.shift()
        }
        return result
    }

    /**
     * @private
     * Maps a topic to a map controlled by an input pattern supporting wildcards in input and map
     * @param {string} topic - The topic to map
     * @param {string} pattern - The input pattern
     * @param {string} map - The output pattern
     * @returns {string} The mapped (transformed) topic
     */
    private _createMapResult(topic: string, pattern: string, map: string): string {
        const topicChunks = topic.split('/')
        const patternChunks = pattern.split('/')
        const mapChunks = map.split('/')

        const result = mapChunks.map((mapChunk, index) => {
            const topicChunk = topicChunks[index]
            if (mapChunk === '#') {
                return this._getTopicEnd(topicChunks, patternChunks)
            } else if (mapChunk === '%') {
                return topicChunk
            } else {
                return mapChunk
            }
        }).join('/')
    
        return result
    }

    /**
     * Recursively sets the internal and external mappings of topics based on a configuration object.
     * @private
     * @param {ITopicMapConfiguration} configuration The configuration object
     */
    private _setMapsRec(configuration: ITopicMapConfiguration): void {
        for (const key in configuration) {
            const value = configuration[key]
            if (Types.isString(value)) {
                this._internalToExternal[key] = value
                this._externalToInternal[value] = key
            } else if (Types.isObject(value)) {
                this._setMapsRec(value)
            }
        }
    }

    /**
     * Tries to map a topic to one of the topic map entries of a patternMap array and returns the mapped result.
     * @param {string} topic topic to map
     * @param {Record<string, string>} patternMap key/value map of topic mappings
     * @returns {string} the mapped (transformed) topic
     * @private
     */
    private _map(topic: string, patternMap: Record<string, string>): string {
        let result = topic
        for (const pattern in patternMap) {
            const topicMatch = new TopicMatch()
            topicMatch.addPattern(pattern, 0)
            if (topicMatch.getFirstMatch(topic) !== undefined) {
                const map = patternMap[pattern]
                result = this._createMapResult(topic, pattern, map)
                break
            }
        }
        return result
    }

    /**
     * Maps a topic from internal representation to external representation.
     * @param {string} topic internal topic to map
     * @returns {string} external topic
     */
    mapInternalToExternal(topic: string): string {
        const result = this._map(topic, this._internalToExternal)
        return result
    }

    /**
     * Maps a topic from external representation to internal representation.
     * @param {string} topic external topic to map
     * @returns {string} internal topic
     */
    mapExternalToInternal(topic: string): string {
        const result = this._map(topic, this._externalToInternal)
        return result
    }
}
