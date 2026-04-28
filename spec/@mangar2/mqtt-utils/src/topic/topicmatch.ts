/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { Types } from '@mangar2/utils';
import { PatternTree } from './patterntree';

/**
 * Class for matching topics with a list of patterns.
 * The pattern is based on the MQTT pattern design. 
 * Each pattern has an associated value (used for QoS in MQTT)
 * @example
 * const topicMatch = new TopicMatch()
 * topicMatch.addPattern('#', 0)
 * topicMatch.getFirstMatch('hello') // returns 0
 * topicMatch.removePattern('#')
 * topicMatch.addPattern('/+', 1)
 * topicMatch.getFirstMatch('hello') // returns undefined
 * topicMatch.getFirstMatch('/hello') // returns 1
 * topicMatch.getFirstMatch('/hello/world') // returns undefined
 */
export class TopicMatch {
    private _tree: PatternTree;
    private _topicPatternList: Record<string, any>;

    /**
     * Contructs a new TopicMatch instance.
     * @param pattern {Object|Array<string>|string} pattern data structure {pattern: value, pattern: value, ...} or list of patterns
     */
    constructor(pattern: Record<string, any> | Array<string> | string = {}) {
        this._tree = new PatternTree();
        this._topicPatternList = {};
        this.addPattern(pattern);
    }

    /** @returns a JSON object representation excluding the internal pattern tree */
    toJSON(): Record<string, any> {
        return {
            _topicPatternList: this._topicPatternList
        }
    }

    /** @returns list of active pattern {pattern:QoS, ...} */
    get topicPatternList(): Record<string, any> { return this._topicPatternList }

    /** Clears the pattern tree */
    clear(): void {
        this._tree = new PatternTree();
    }

    /**
     * Adds a pattern to the pattern list if it's not already present.
     * 
     * This method accepts three types of inputs:
     * - A string: the pattern to add.
     * - An array of strings: each string is added as a separate pattern.
     * - An object: each key-value pair is treated as a pattern and its associated value.
     * 
     * @param pattern A single pattern (as a string), or a list of patterns (as an array or an object)
     * @param value The value to associate with the pattern (defaults to 0)
     * @throws If the pattern is not a string, array, or object
     */
    addPattern(pattern: string | Array<string> | Record<string, any>, value: any = 0): void {
        if (Types.isString(pattern)) {
            this._topicPatternList[pattern] = value
            this._tree.addPattern(pattern, value)
        } else if (Types.isArray(pattern)) {
            for (const topic of pattern) {
                this._topicPatternList[topic] = value
                this._tree.addPattern(topic, value)
            }
        } else {
            this._topicPatternList = { ...this.topicPatternList, ...pattern }
            for (const topic in pattern) {
                this._tree.addPattern(topic, pattern[topic])
            }
        } 
    }

    /**
     * Deletes a pattern or multiple patterns from the tree.
     * 
     * @param patternList The pattern(s) to delete. This can be a string or an array of strings.
     * @throws If patternList is undefined
     */
    deletePattern(patternList: string | Array<string>): void {
        if (patternList === undefined) {
            throw new Error('undefined pattern in removePattern')
        }

        if (!Array.isArray(patternList)) {
            patternList = [patternList]
        }

        for (const pattern of patternList) {
            if (pattern === '#') {
                this._tree = new PatternTree()
            } else {
                this._tree.deletePattern(pattern)
            }
        }
    }

    /**
     * Changes the set of patterns by setting, adding, or removing.
     *
     * @param patternCommand An object with attributes for setting, removing, and adding patterns.
     */
    changePattern(patternCommand: 
        { set?: { pattern: string | Array<string>, value: any }, remove?: string | Array<string>, add?: { pattern: string | Array<string>, value: any } }): 
        void {
        if (patternCommand.set !== undefined) {
            this._tree = new PatternTree()
            this.addPattern(patternCommand.set.pattern, patternCommand.set.value)
        }

        if (patternCommand.remove !== undefined) {
            this.deletePattern(patternCommand.remove)
        }

        if (patternCommand.add !== undefined) {
            this.addPattern(patternCommand.add.pattern, patternCommand.add.value)
        }
    }

    /**
     * Returns the value of the first matching pattern.
     *
     * @param searchTopic The topic to search for
     * @return The value associated with the found topic, or undefined if no match was found
     */
    getFirstMatch(searchTopic: string): any | undefined {
        return this._tree.getFirstMatch(searchTopic)
    }

    /**
     * Returns the best value of all matching patterns according to the provided comparison function.
     *
     * If no comparison function is provided, the values of the patterns are compared using the ">" operator.
     *
     * @param searchTopic The topic to search for
     * @param isBetter A function to decide if "a" is better than "b". Defaults to a function that returns true if a > b.
     * @return The value associated with the best found topic, or undefined if no match was found.
     */
    getBestMatch(searchTopic: string, isBetter: (a: any, b: any) => boolean = (a, b) => a > b): any | undefined {
        return this._tree.getBestMatch(searchTopic, isBetter)
    }


}

