export default class CheckInput {
    public messages: string[] = [];

    constructor(_schema: unknown) {
        // Minimal runtime shim for integration flow.
    }

    validate(_value: unknown): boolean {
        this.messages = [];
        return true;
    }
}
