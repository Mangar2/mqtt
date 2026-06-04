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

const ensurePublishResult = (result: string[], qos: number): void => {
    if (!Array.isArray(result) || result.length === 0) {
        throw new Error(`publish(qos=${qos}) returned empty result`);
    }

    const errorEntry = result.find((entry) => entry.toLowerCase().includes("error publishing message"));
    if (errorEntry) {
        throw new Error(`publish(qos=${qos}) failed: ${errorEntry}`);
    }
};

const main = async (): Promise<void> => {
    const [httpHost, httpPortText] = process.argv.slice(2);
    if (!httpHost || !httpPortText) {
        throw new Error("usage: flow_runner.js <http-host> <http-port>");
    }

    const httpPort = Number.parseInt(httpPortText, 10);
    if (!Number.isFinite(httpPort) || httpPort <= 0) {
        throw new Error(`invalid http port: ${httpPortText}`);
    }

    const clientId = `ts-http-it-${Date.now()}`;
    const topic = `integration/http_mqtt_interface_client/typescript/${Date.now()}`;
    const receivedMessages: IMessage[] = [];

    const services = new HttpClient({
        clientId,
        brokerOptions: {
            host: httpHost,
            port: httpPort,
        },
        version: "1.0",
        listener: 0,
    });

    services.onPublish((message) => {
        receivedMessages.push(message.clone());
    });

    services.start();

    let sendToken = "";
    try {
        const connectResult = await services.connect(true);
        sendToken = connectResult.token.send;
        if (!sendToken) {
            throw new Error("connect did not return send token");
        }

        const subscribeResult = await services.subscribe({ [topic]: 2 });
        if (!Array.isArray(subscribeResult.qos) || subscribeResult.qos.length === 0) {
            throw new Error("subscribe returned empty qos list");
        }

        const qos0Result = await services.publish(sendToken, new Message(topic, "payload-qos0", "ts-it", 0, false), "ts-it");
        ensurePublishResult(qos0Result, 0);
        const qos1Result = await services.publish(sendToken, new Message(topic, "payload-qos1", "ts-it", 1, false), "ts-it");
        ensurePublishResult(qos1Result, 1);
        const qos2Result = await services.publish(sendToken, new Message(topic, "payload-qos2", "ts-it", 2, false), "ts-it");
        ensurePublishResult(qos2Result, 2);

        await waitForCondition(() => {
            const payloads = receivedMessages.map((message) => String(message.value));
            return payloads.includes("payload-qos0")
                && payloads.includes("payload-qos1")
                && payloads.includes("payload-qos2");
        }, 5000);

        await services.pingreq(sendToken);
        await services.unsubscribe({ [topic]: 2 });
        await services.disconnect();

        const result = {
            ok: true,
            clientId,
            topic,
            receivedCount: receivedMessages.length,
            receivedPayloads: receivedMessages.map((message) => String(message.value)),
        };
        process.stdout.write(JSON.stringify(result));
    } finally {
        services.close();
    }
};

void main().catch((error: unknown) => {
    const message = error instanceof Error ? error.message : String(error);
    process.stderr.write(message);
    process.exit(1);
});
