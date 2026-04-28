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

const CheckInput = require('@mangar2/checkinput')

const checkRule = new CheckInput({
    type: 'object',
    properties: {
        name: { type: 'string' },
        doLog: { type: ['boolean', 'number'] },
        active: { type: ['boolean']},
        isValid: { 
            description: 'true, if the rule has been validated, false, if validation produces an error',
            type: 'boolean' 
        },
        errors: {
            description: 'list of error messages',
            type: ['string', 'object', 'array']
        },
        durationWithoutMovementInMinutes: { type: 'number' },
        allOf: {
            type: ['array', 'string'],
            minItems: 1,
            items: { type: 'string' }
        },
        anyOf: {
            type: ['array', 'string'],
            minItems: 1,
            items: { type: 'string' }
        },
        allow: {
            type: 'array',
            minItems: 1,
            items: { type: 'string' }
        },
        noneOf: {
            type: ['array', 'string'],
            minItems: 1,
            items: { type: 'string' }
        },
        topic: { $ref: '#/definitions/topic' },
        check: { $ref: '#/definitions/check' },
        value: { $ref: '#/definitions/check' },
        time: {
            allOf: [
                {
                    type: ['string', 'array']
                },
                {
                    $ref: '#/definitions/check'
                }
            ]
        },
        weekdays: {
            anyOf: [
                {
                    type: 'string',
                    enum: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']
                },
                {
                    type: 'array',
                    items: {
                        type: 'string',
                        enum: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']
                    }
                }
            ]
        },
        duration: { type: ['number', 'string'] },
        cooldownInSeconds: {
            type: ['integer'],
            minimum: 0
        },
        delayInSeconds: {
            type: ['integer'],
            minimum: 0
        },
        qos: { enum: [0, 1, 2] }
    },
    required: ['topic'],
    additionalProperties: false,
    definitions: {
        topic: {
            if: {
                required: ['value']
            },
            then: {
                type: ['string', 'array'],
                minItems: 1,
                items: { type: 'string' }
            },
            else: {
                type: ['object'],
                minProperties: 1,
                additionalProperties: { $ref: '#/definitions/check' }
            }
        },
        check: {
            type: ['number', 'string', 'array'],
            anyOf: [
                {
                    items: [
                        {
                            enum: ['and', 'or']
                        }
                    ],
                    additionalItems: { $ref: '#/definitions/check' }
                },
                {
                    items: [
                        {
                            enum: [
                                '+', '-',
                                '=', 'equal', 'equals',
                                '!=', 'not equal', 'not equals',
                                '<', 'below', 'less',
                                '>', 'above', 'greater',
                                '<=', 'below or equal', 'less or equal',
                                '>=', 'above or equal', 'greater or equal'
                            ]
                        },
                        { $ref: '#/definitions/check' },
                        { $ref: '#/definitions/check' }
                    ]
                },
                {
                    items: [
                        {
                            enum: ['switch']
                        },
                        {
                            $ref: '#/definitions/check'
                        },
                        {
                            type: 'object',
                            additionalProperties: { type: ['string', 'number', 'boolean'] }
                        }
                    ],
                    additionalItems: false
                },
                {
                    items: [
                        {
                            enum: ['if']
                        },
                        { $ref: '#/definitions/check' },
                        { $ref: '#/definitions/check' },
                        { $ref: '#/definitions/check' }
                    ],
                    additionalItems: false
                },
                {
                    items: [
                        {
                            enum: ['call']
                        },
                        {
                            type: 'string'
                        }
                    ]
                }
            ]
        }
    }

})

module.exports = checkRule
