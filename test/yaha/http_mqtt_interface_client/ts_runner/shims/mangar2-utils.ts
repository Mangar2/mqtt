type CallbackFunction = (...args: any[]) => any;

export class Types {
    static isString(value: unknown): value is string {
        return typeof value == "string";
    }

    static isError(value: unknown): value is Error {
        return value instanceof Error;
    }

    static isNumber(value: unknown): value is number {
        return typeof value == "number" && Number.isFinite(value);
    }

    static isArray(value: unknown): value is unknown[] {
        return Array.isArray(value);
    }

    static isAnyFunction(value: unknown): value is CallbackFunction {
        return typeof value == "function";
    }

    static isAsyncFunction(value: unknown): value is CallbackFunction {
        return typeof value == "function";
    }

    static isObject(value: unknown): value is Record<string, unknown> {
        return value !== null && typeof value == "object" && !Array.isArray(value);
    }
}

export class Callbacks {
    private callbacks: Record<string, CallbackFunction[]> = {};

    constructor(events: string[]) {
        for (const eventName of events) {
            this.callbacks[eventName.toLowerCase()] = [];
        }
    }

    on(event: string, callback: CallbackFunction): void {
        const normalized = event.toLowerCase();
        if (!(normalized in this.callbacks)) {
            throw new Error(`event not supported: ${event}`);
        }
        if (typeof callback != "function") {
            throw new Error("callback must be a function");
        }
        this.callbacks[normalized].push(callback);
    }

    invokeCallback(event: string, ...args: any[]): any {
        const normalized = event.toLowerCase();
        if (!(normalized in this.callbacks)) {
            throw new Error(`event not supported: ${event}`);
        }
        let result: any;
        for (const callback of this.callbacks[normalized]) {
            result = callback(...args);
        }
        return result;
    }

    async invokeCallbackAsync(event: string, ...args: any[]): Promise<any> {
        const normalized = event.toLowerCase();
        if (!(normalized in this.callbacks)) {
            throw new Error(`event not supported: ${event}`);
        }
        let result: any;
        for (const callback of this.callbacks[normalized]) {
            result = await callback(...args);
        }
        return result;
    }
}

export const delay = async (milliseconds: number): Promise<void> => {
    await new Promise<void>((resolve) => {
        setTimeout(resolve, milliseconds);
    });
};

export const shutdown = (_callback: () => Promise<void> | void): void => {
    // Integration harness does not require process signal wiring.
};

export const errorLog = (_error: unknown, _debug: boolean = false): void => {
    // Integration harness intentionally keeps shim logger silent.
};

export type StringIndexed = Record<string, unknown>;

export const deepMerge = <T>(baseValue: T, overrideValue: Partial<T>): T => {
    if (!Types.isObject(baseValue) || !Types.isObject(overrideValue)) {
        return (overrideValue as T) ?? baseValue;
    }

    const merged: Record<string, unknown> = { ...(baseValue as Record<string, unknown>) };
    for (const [key, value] of Object.entries(overrideValue)) {
        const previous = merged[key];
        if (Types.isObject(previous) && Types.isObject(value)) {
            merged[key] = deepMerge(previous, value);
        } else {
            merged[key] = value;
        }
    }
    return merged as T;
};
