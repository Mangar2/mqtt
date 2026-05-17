'use strict';

const fs = require('node:fs');
const path = require('node:path');

const RS485State = require('../../../../spec/@mangar2/rs485interface/rs485state');
const constants = require('../../../../spec/@mangar2/rs485interface/constants');

const LOOP_START = 11;
const LOOP_SHORT_BREAK = 12;
const LOOP_LONG_BREAK = 13;
const LOOP_TIMEOUT = 10;

function createState() {
    return new RS485State();
}

function snapshot(result, state) {
    const rightSibling = state.rightSibling === null ? -1 : state.rightSibling;
    const leftmostSibling = state.leftmostSibling === null ? -1 : state.leftmostSibling;
    return {
        result,
        state: state._state,
        timer: state._timer,
        maySend: Boolean(state.maySend),
        rightSibling,
        leftmostSibling,
        tokenLost: Boolean(state.tokenLost)
    };
}

function applyEvent(state, event) {
    if (event.kind === 'tick') {
        return snapshot(state.updateStateNoMessage(), state);
    }

    return snapshot(state.updateState(event.request, event.notForMe), state);
}

function runCase(name, events) {
    const state = createState();
    const expected = events.map((event) => applyEvent(state, event));
    return { name, events, expected };
}

function ticksUntilState(targetState, maxTicks) {
    const state = createState();
    const sequence = [];
    for (let index = 0; index < maxTicks; index += 1) {
        sequence.push({ kind: 'tick', request: 0, notForMe: false });
        state.updateStateNoMessage();
        if (state._state === targetState) {
            return sequence;
        }
    }

    throw new Error(`failed to reach state ${targetState} within ${maxTicks} ticks`);
}

function buildTransitionMatrixCases() {
    const requests = [
        constants.ENABLE_SEND,
        constants.REGISTRATION_INFO,
        constants.REGISTRATION_REQUEST,
        LOOP_START,
        LOOP_SHORT_BREAK,
        LOOP_LONG_BREAK,
        LOOP_TIMEOUT
    ];

    const stateSetups = [
        { name: 'unknown', setup: [] },
        { name: 'reboot', setup: ticksUntilState(constants.STATE_REBOOT, 120) },
        { name: 'single', setup: ticksUntilState(constants.STATE_SINGLE, 240) },
        {
            name: 'unregistered',
            setup: [{ kind: 'message', request: constants.REGISTRATION_REQUEST, notForMe: false }]
        },
        {
            name: 'registered',
            setup: [{ kind: 'message', request: constants.ENABLE_SEND, notForMe: false }]
        }
    ];

    const cases = [];
    for (const stateSetup of stateSetups) {
        for (const request of requests) {
            for (const notForMe of [false, true]) {
                cases.push(runCase(
                    `matrix_${stateSetup.name}_request_${request}_notForMe_${notForMe ? '1' : '0'}`,
                    [...stateSetup.setup, { kind: 'message', request, notForMe }]
                ));
            }
        }
    }

    return cases;
}

function buildLongReplayCase() {
    const events = [];

    for (let index = 0; index < 130; index += 1) {
        events.push({ kind: 'tick', request: 0, notForMe: false });
    }

    events.push({ kind: 'message', request: constants.ENABLE_SEND, notForMe: false });
    events.push({ kind: 'message', request: constants.ENABLE_SEND, notForMe: true });

    for (let index = 0; index < 55; index += 1) {
        events.push({ kind: 'tick', request: 0, notForMe: false });
    }

    events.push({ kind: 'message', request: constants.REGISTRATION_INFO, notForMe: false });
    events.push({ kind: 'message', request: constants.REGISTRATION_REQUEST, notForMe: true });

    for (let index = 0; index < 30; index += 1) {
        events.push({ kind: 'tick', request: 0, notForMe: false });
    }

    events.push({ kind: 'message', request: LOOP_START, notForMe: false });
    events.push({ kind: 'message', request: LOOP_SHORT_BREAK, notForMe: false });
    events.push({ kind: 'message', request: LOOP_LONG_BREAK, notForMe: false });
    events.push({ kind: 'message', request: constants.ENABLE_SEND, notForMe: false });

    return runCase('long_replay', events);
}

function renderFixture(cases) {
    const lines = [];
    for (const parityCase of cases) {
        lines.push(`CASE|${parityCase.name}`);
        for (let index = 0; index < parityCase.events.length; index += 1) {
            const event = parityCase.events[index];
            const expected = parityCase.expected[index];
            lines.push([
                'STEP',
                event.kind === 'tick' ? '1' : '0',
                String(event.request),
                event.notForMe ? '1' : '0',
                String(expected.result),
                String(expected.state),
                String(expected.timer),
                expected.maySend ? '1' : '0',
                String(expected.rightSibling),
                String(expected.leftmostSibling),
                expected.tokenLost ? '1' : '0'
            ].join('|'));
        }
        lines.push('END');
    }

    return `${lines.join('\n')}\n`;
}

function main() {
    const matrixCases = buildTransitionMatrixCases();
    const longReplayCase = buildLongReplayCase();
    const allCases = [...matrixCases, longReplayCase];

    const fixtureText = renderFixture(allCases);
    const outputPath = path.join(__dirname, 'rs485_state_parity_fixture.txt');
    fs.writeFileSync(outputPath, fixtureText, 'utf8');

    process.stdout.write(`generated ${allCases.length} parity cases\n`);
}

main();
