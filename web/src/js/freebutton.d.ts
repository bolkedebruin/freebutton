/**
 * FreeButton Global Type Declarations
 * Auto-generated from stdlib.json
 * DO NOT EDIT MANUALLY - Run: npm run generate-types
 */

export interface ConsoleAPI {
    log(): void;

}

export interface MqttAPI {
    getBrokerCount(): void;

    getBrokerName(): void;

    isConnected(): void;

    publish(): void;

    subscribe(): void;

    unsubscribe(): void;

    onConnect(): void;

    onDisconnect(): void;

}

export interface PerformanceAPI {
    now(): void;

}

export interface SensorAPI {
    count(): void;

    getValue(): void;

    getType(): void;

    getInfo(): void;

    getAll(): void;

    onChange(): void;

}

export interface SystemAPI {
    uptime(): void;

    freeHeap(): void;

    version(): void;

}

export interface ArrayConstructor {
    isArray(): any;
}

export interface Array {
    concat(): any;
    push(): any;
    pop(): any;
    join(): any;
    toString(): any;
    reverse(): any;
    shift(): any;
    slice(): any;
    splice(): any;
    unshift(): any;
    indexOf(): any;
    lastIndexOf(): any;
    every(): any;
    some(): any;
    forEach(): any;
    map(): any;
    filter(): any;
    reduce(): any;
    reduceRight(): any;
    reduce(): any;
    sort(): any;
}

export interface DateConstructor {
    now(): any;
}

export interface Error {
    toString(): any;
}

export interface Function {
    call(): any;
    apply(): any;
    bind(): any;
    toString(): any;
}

export interface JSONConstructor {
    parse(): any;
    stringify(): any;
}

export interface MathConstructor {
    min(): any;
    max(): any;
    sign(): any;
    abs(): any;
    floor(): any;
    ceil(): any;
    round(): any;
    sqrt(): any;
    sin(): any;
    cos(): any;
    tan(): any;
    asin(): any;
    acos(): any;
    atan(): any;
    atan2(): any;
    exp(): any;
    log(): any;
    pow(): any;
    random(): any;
    imul(): any;
    clz32(): any;
    fround(): any;
    trunc(): any;
    log2(): any;
    log10(): any;
}

export interface NumberConstructor {
    parseInt(): any;
    parseFloat(): any;
}

export interface Number {
    toExponential(): any;
    toFixed(): any;
    toPrecision(): any;
    toString(): any;
}

export interface ObjectConstructor {
    defineProperty(): any;
    getPrototypeOf(): any;
    setPrototypeOf(): any;
    create(): any;
    keys(): any;
    freeze(): any;
    seal(): any;
    preventExtensions(): any;
    isFrozen(): any;
    isSealed(): any;
    isExtensible(): any;
}

export interface Object {
    hasOwnProperty(): any;
    toString(): any;
}

export interface RegExp {
    exec(): any;
    test(): any;
}

export interface StringConstructor {
    fromCharCode(): any;
    fromCodePoint(): any;
}

export interface String {
    charAt(): any;
    charCodeAt(): any;
    codePointAt(): any;
    slice(): any;
    substring(): any;
    concat(): any;
    indexOf(): any;
    lastIndexOf(): any;
    match(): any;
    replace(): any;
    replaceAll(): any;
    search(): any;
    split(): any;
    toLowerCase(): any;
    toUpperCase(): any;
    trim(): any;
    trimEnd(): any;
    trimStart(): any;
    toString(): any;
    repeat(): any;
}

// ============================================================================
// Global Variables and Functions
// ============================================================================

declare global {
    const console: ConsoleAPI;
    const mqtt: MqttAPI;
    const performance: PerformanceAPI;
    const sensor: SensorAPI;
    const system: SystemAPI;

    const Array: ArrayConstructor;
    const Date: DateConstructor;
    const JSON: JSONConstructor;
    const Math: MathConstructor;
    const Number: NumberConstructor;
    const Object: ObjectConstructor;
    const String: StringConstructor;

    /** Hierarchical device API mirroring physical topology. Built at runtime in C. */
    const fb: FbRoot;

    // Global constants
    /** The undefined value */
    const undefined: undefined;
    /** Positive infinity value */
    const Infinity: number;
    /** Not-a-Number value */
    const NaN: number;
    /** The global object */
    const globalThis: object;

    // Global functions
    function parseInt(): any;
    function parseFloat(): any;
    function eval(): any;
    function isNaN(): any;
    function isFinite(): any;
    function print(): any;
    function gc(): any;
    function load(): any;
    function loadMapped(): any;
    function loadUserBytecode(): any;
    function setTimeout(): any;
    function clearTimeout(): any;

    // Test utility functions
    /** Throws an error with the given message (test utility) */
    function throw_error(msg: string): never;
    /** Asserts that actual equals expected, throws error if not (test utility) */
    function assert(actual: any, expected?: any, message?: string): void;
    /** Asserts that a function throws the expected error type (test utility) */
    function assert_throws(expected_error: any, func: Function): void;
}

// ============================================================================
// FreeButton Device Hierarchy (fb global)
// ============================================================================

export interface FbBar {
    /** Left side */
    readonly left: FbSide;
    /** Right side */
    readonly right: FbSide;
}

export interface FbButton {
    /** Register click handler */
    onClick(callback: Function): void;
    /** Register long press handler */
    onLongPress(callback: Function): void;
    /** Register release handler */
    onRelease(callback: Function): void;
}

export interface FbLed {
    /** Set LED RGB color */
    setColor(r: number, g: number, b: number): void;
    /** Turn LED on (white) */
    on(): void;
    /** Turn LED off */
    off(): void;
}

export interface FbMain {
    /** Main TFT display (display items) */
    readonly display: FbMainDisplay;
    /** Left side (button + display; main has no LEDs) */
    readonly left: FbMainSide;
    /** Right side (button + display; main has no LEDs) */
    readonly right: FbMainSide;
}

export interface FbMainDisplay {
    /** Create a display item */
    create(type: string): number;
    /** Remove a display item */
    remove(id: number): boolean;
    /** Get number of display items */
    count(): number;
    /** Get display item by ID */
    get(id: number): object;
    /** Get all display items */
    getAll(): Array;
    /** Set item label */
    setLabel(id: number, label: string): void;
    /** Set item value */
    setValue(id: number, value: any): void;
    /** Set item unit */
    setUnit(id: number, unit: string): void;
    /** Set item position */
    setPosition(id: number, x: number, y: number): void;
    /** Set item page */
    setPage(id: number, page: number): void;
}

export interface FbMainSide {
    /** Physical button */
    readonly button: FbButton;
    /** Button display */
    readonly display: FbSmallDisplay;
}

export interface FbSide {
    /** Front-facing LED */
    readonly led: FbLed;
    /** Wall-facing LED */
    readonly backLed: FbLed;
    /** Physical button */
    readonly button: FbButton;
    /** Button display */
    readonly display: FbSmallDisplay;
}

export interface FbSmallDisplay {
    /** Set button display label */
    setLabel(text: string): void;
    /** Set button display top label */
    setTopLabel(text: string): void;
}

export interface FbRoot {
    /** Main display unit */
    readonly main: FbMain;
    /** Connected bar modules */
    readonly bars: FbBar[];
}


// ============================================================================
// Compiler API (loaded via WASM)
// ============================================================================

export interface CompileOptions {
    targetAddr?: number;
    use32Bit?: boolean;
}

export interface CompilerVersion {
    compiler: string;
    features: string[];
    platform: string;
}

export interface MQuickJSCompiler {
    ready: boolean;
    init(): Promise<void>;
    compile(sourceCode: string, options?: CompileOptions): Promise<Uint8Array>;
    validate(sourceCode: string): Promise<boolean>;
    getVersion(): CompilerVersion;
}

declare global {
    const mqjsCompiler: MQuickJSCompiler;
}
