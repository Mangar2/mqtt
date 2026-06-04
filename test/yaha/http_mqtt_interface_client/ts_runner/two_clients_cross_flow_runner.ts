import { HttpClient } from "@mangar2/mqtt-client/src/http-services/http-client-services";
import { Message, IMessage } from "@mangar2/mqtt-utils";

const waitForCondition = async (predicate: () => boolean, timeoutMs: number): Promise<void> => {
    const startedAt = Date.now();
    while (Date.now() - startedAt < timeoutMs) {
        if (predicate()) {
            return;
        }
        await new Promise<void>((resolve) => setTimeout(resolve, 25));
    }
    throw new Error("timeout while waiting for expected message event");
};

const ensurePublishResult = (result: string[], label: string): void => {
    if (!Array.isArray(result) || result.length == 0) {
        throw new Error(`${label}: publish returned empty result`);
    }

    const errorEntry = result.find((entry) => entry.toLowerCase().includes("error publishing message"));
    if (errorEntry) {
        throw new Error(`${label}: ${errorEntry}`);
    }
};

const createClient = (clientId: string, httpHost: string, httpPort: number): HttpClient => {
    return new HttpClient({
        clientId,
        brokerOptions: {
            host: httpHost,
            port: httpPort,
        },
        version: "1.0",
        listener: 0,
    });
};

const main = async (): Promise<void> => {
    const [httpHost, httpPortText] = process.argv.slice(2);
    if (!httpHost || !httpPortText) {
        throw new Error("usage: two_clients_cross_flow_runner.js <http-host> <http-port>");
    }

    const httpPort = Number.parseInt(httpPortText, 10);
    if (!Number.isFinite(httpPort) || httpPort <= 0) {
        throw new Error(`invalid http port: ${httpPortText}`);
    }

    const runId = `${Date.now()}`;
    const clientAId = `ts-http-a-${runId}`;
    const clientBId = `ts-http-b-${runId}`;
    const topicForA = `integration/http_mqtt_interface_client/two_clients/${runId}/in/a`;
    const topicForB = `integration/http_mqtt_interface_client/two_clients/${runId}/in/b`;

    const receivedByA: IMessage[] = [];
    const receivedByB: IMessage[] = [];

    const clientA = createClient(clientAId, httpHost, httpPort);
    const clientB = createClient(clientBId, httpHost, httpPort);

    clientA.onPublish((message) => {
        receivedByA.push(message.clone());
    });
    clientB.onPublish((message) => {
        receivedByB.push(message.clone());
    });

    clientA.start();
    clientB.start();

    try {
        const connectA = await clientA.connect(true);
        const connectB = await clientB.connect(true);

        const sendTokenA = connectA.token.send;
        const sendTokenB = connectB.token.send;
        if (!sendTokenA || !sendTokenB) {
            throw new Error("connect did not return send tokens for both clients");
        }

        const subscribeA = await clientA.subscribe({ [topicForA]: 2 });
        const subscribeB = await clientB.subscribe({ [topicForB]: 2 });
        if (!Array.isArray(subscribeA.qos) || subscribeA.qos.length == 0) {
            throw new Error("client A subscribe returned empty qos list");
        }
        if (!Array.isArray(subscribeB.qos) || subscribeB.qos.length == 0) {
            throw new Error("client B subscribe returned empty qos list");
        }

        ensurePublishResult(
            await clientA.publish(sendTokenA, new Message(topicForB, "from-a", "ts-it-a", 1, false), "ts-it-a"),
            "client A publish"
        );
        ensurePublishResult(
            await clientB.publish(sendTokenB, new Message(topicForA, "from-b", "ts-it-b", 1, false), "ts-it-b"),
            "client B publish"
        );

        await waitForCondition(() => {
            const payloadsA = receivedByA.map((message) => String(message.value));
            const payloadsB = receivedByB.map((message) => String(message.value));
            return payloadsA.includes("from-b") && payloadsB.includes("from-a");
        }, 5000);

        const payloadsA = receivedByA.map((message) => String(message.value));
        const payloadsB = receivedByB.map((message) => String(message.value));
        if (payloadsA.includes("from-a")) {
            throw new Error("client A received own payload");
        }
        if (payloadsB.includes("from-b")) {
            throw new Error("client B received own payload");
        }

        await clientA.pingreq(sendTokenA);
        await clientB.pingreq(sendTokenB);

        await clientA.unsubscribe({ [topicForA]: 2 });
        await clientB.unsubscribe({ [topicForB]: 2 });

        await clientA.disconnect();
        await clientB.disconnect();

        process.stdout.write(JSON.stringify({
            ok: true,
            clientA: {
                clientId: clientAId,
                receivedPayloads: payloadsA,
                receivedCount: payloadsA.length,
            },
            clientB: {
                clientId: clientBId,
                receivedPayloads: payloadsB,
                receivedCount: payloadsB.length,
            },
            topicForA,
            topicForB,
        }));
    } finally {
        clientA.close();
        clientB.close();
    }
};

void main().catch((error: unknown) => {
    const message = error instanceof Error ? error.message : String(error);
    process.stderr.write(message);
    process.exit(1);
});
