/**
 * FreeButton Global Type Declarations
 * Auto-generated from stdlib.json
 * DO NOT EDIT MANUALLY - Run: npm run generate-types
 */

export interface ConsoleAPI {
    /** Outputs a message to the console */
    log(args: any): void;

}

export interface MqttAPI {
    /** Get number of configured MQTT brokers */
    getBrokerCount(): number;

    /** Get MQTT broker name */
    getBrokerName(brokerId: number): string;

    /** Check if MQTT broker is connected */
    isConnected(brokerId: number): boolean;

    /** Publish MQTT message to topic */
    publish(brokerId: number, topic: string, payload: string, qos: number, retain: number): void;

    /** Subscribe to MQTT topic with callback */
    subscribe(brokerId: number, topic: string, callback: Function, qos: number): void;

    /** Unsubscribe from MQTT topic */
    unsubscribe(brokerId: number, topic: string): void;

    /** Register callback for MQTT broker connection event */
    onConnect(brokerId: number, callback: Function): void;

    /** Register callback for MQTT broker disconnection event */
    onDisconnect(brokerId: number, callback: Function): void;

}

export interface PerformanceAPI {
    /** Returns a high-resolution timestamp in milliseconds */
    now(): number;

}

export interface SensorAPI {
    /** Get number of available sensors */
    count(): number;

    /** Read current sensor value */
    getValue(sensorId: number): number;

    /** Get sensor type name */
    getType(sensorId: number): string;

    /** Get sensor information object */
    getInfo(sensorId: number): object;

    /** Iterate over every sensor, invoking callback(id, type) once per sensor. Memory-safe alternative to a getAll() array: it allocates nothing up front (important on the small script heap). Use sensor.getValue(id) / sensor.getInfo(id) inside the callback if you need more than id + type. */
    forEach(callback: Function): void;

    /** Register change event handler for sensor value changes */
    onChange(sensorId: number, callback: Function): void;

}

export interface SystemAPI {
    /** Get device uptime in seconds */
    uptime(): number;

    /** Get free heap memory in bytes */
    freeHeap(): number;

    /** Get firmware version string */
    version(): string;

}

export interface ArrayConstructor {
    /** Determines whether the passed value is an Array */
    isArray(value: any): boolean;
}

export interface Array {
    /** Merges two or more arrays */
    concat(array: any[]): any[];
    /** Adds one or more elements to the end of an array */
    push(element: any): number;
    /** Removes the last element from an array and returns that element */
    pop(): any;
    /** Joins all elements of an array into a string */
    join(separator: string): string;
    /** Returns a string representing the specified array and its elements */
    toString(): string;
    /** Reverses an array in place */
    reverse(): any[];
    /** Removes the first element from an array and returns that element */
    shift(): any;
    /** Returns a shallow copy of a portion of an array */
    slice(start: number, end: number): any[];
    /** Changes the contents of an array by removing or replacing elements */
    splice(start: number, deleteCount: number, item: any): any[];
    /** Adds one or more elements to the beginning of an array */
    unshift(element: any): number;
    /** Returns the first index at which a given element can be found */
    indexOf(searchElement: any, fromIndex: number): number;
    /** Returns the last index at which a given element can be found */
    lastIndexOf(searchElement: any, fromIndex: number): number;
    /** Tests whether all elements pass the test implemented by the provided function */
    every(callback: Function, thisArg: any): boolean;
    /** Tests whether at least one element passes the test */
    some(callback: Function, thisArg: any): boolean;
    /** Executes a provided function once for each array element */
    forEach(callback: Function, thisArg: any): void;
    /** Creates a new array with the results of calling a function for every element */
    map(callback: Function, thisArg: any): any[];
    /** Creates a new array with all elements that pass the test */
    filter(callback: Function, thisArg: any): any[];
    /** Executes a reducer function on each element, resulting in a single output value */
    reduce(callback: Function, initialValue: any): any;
    /** Applies a function against an accumulator and each value (from right-to-left) */
    reduceRight(callback: Function, initialValue: any): any;
    /** Executes a reducer function on each element, resulting in a single output value */
    reduce(callback: Function, initialValue: any): any;
    /** Sorts the elements of an array in place */
    sort(compareFunction: Function): any[];
}

export interface DateConstructor {
    /** Returns the number of milliseconds elapsed since January 1, 1970 */
    now(): number;
}

export interface Error {
    /** Returns a string representing the error */
    toString(): string;
}

export interface Function {
    /** Calls a function with a given this value and arguments */
    call(thisArg: any, arg: any): any;
    /** Calls a function with a given this value and arguments provided as an array */
    apply(thisArg: any, argsArray: any[]): any;
    /** Creates a new function that has its this keyword set to the provided value */
    bind(thisArg: any, arg: any): Function;
    /** Returns a string representing the source code of the function */
    toString(): string;
}

export interface JSONConstructor {
    /** Parses a JSON string, constructing the JavaScript value or object */
    parse(text: string, reviver: Function): any;
    /** Converts a JavaScript value to a JSON string */
    stringify(value: any, replacer: Function|any[], space: string|number): string;
}

export interface MathConstructor {
    /** Returns the smallest of zero or more numbers */
    min(value: number): number;
    /** Returns the largest of zero or more numbers */
    max(value: number): number;
    /** Returns the sign of a number */
    sign(x: number): number;
    /** Returns the absolute value of a number */
    abs(x: number): number;
    /** Returns the largest integer less than or equal to a number */
    floor(x: number): number;
    /** Returns the smallest integer greater than or equal to a number */
    ceil(x: number): number;
    /** Returns the value of a number rounded to the nearest integer */
    round(x: number): number;
    /** Returns the square root of a number */
    sqrt(x: number): number;
    /** Returns the sine of a number */
    sin(x: number): number;
    /** Returns the cosine of a number */
    cos(x: number): number;
    /** Returns the tangent of a number */
    tan(x: number): number;
    /** Returns the arcsine of a number */
    asin(x: number): number;
    /** Returns the arccosine of a number */
    acos(x: number): number;
    /** Returns the arctangent of a number */
    atan(x: number): number;
    /** Returns the arctangent of the quotient of its arguments */
    atan2(y: number, x: number): number;
    /** Returns E raised to the power of a number */
    exp(x: number): number;
    /** Returns the natural logarithm of a number */
    log(x: number): number;
    /** Returns base to the exponent power */
    pow(base: number, exponent: number): number;
    /** Returns a pseudo-random number between 0 and 1 */
    random(): number;
    /** Returns the result of the 32-bit integer multiplication */
    imul(a: number, b: number): number;
    /** Returns the number of leading zero bits in the 32-bit binary representation */
    clz32(x: number): number;
    /** Returns the nearest 32-bit single precision float representation */
    fround(x: number): number;
    /** Returns the integer part of a number */
    trunc(x: number): number;
    /** Returns the base 2 logarithm of a number */
    log2(x: number): number;
    /** Returns the base 10 logarithm of a number */
    log10(x: number): number;
}

export interface NumberConstructor {
    /** Parses a string argument and returns an integer */
    parseInt(string: string, radix: number): number;
    /** Parses a string argument and returns a floating point number */
    parseFloat(string: string): number;
}

export interface Number {
    /** Returns a string representing the number in exponential notation */
    toExponential(fractionDigits: number): string;
    /** Formats a number using fixed-point notation */
    toFixed(digits: number): string;
    /** Returns a string representing the number to a specified precision */
    toPrecision(precision: number): string;
    /** Returns a string representing the specified number object */
    toString(radix: number): string;
}

export interface ObjectConstructor {
    /** Defines a new property or modifies an existing property on an object */
    defineProperty(obj: object, prop: string, descriptor: object): object;
    /** Returns the prototype of the specified object */
    getPrototypeOf(obj: object): object;
    /** Sets the prototype of a specified object */
    setPrototypeOf(obj: object, prototype: object): object;
    /** Creates a new object with the specified prototype object */
    create(proto: object): object;
    /** Returns an array of a given object's own enumerable property names */
    keys(obj: object): string[];
    freeze(): any;
    seal(): any;
    preventExtensions(): any;
    isFrozen(): any;
    isSealed(): any;
    isExtensible(): any;
}

export interface Object {
    /** Returns a boolean indicating whether the object has the specified property */
    hasOwnProperty(prop: string): boolean;
    /** Returns a string representation of the object */
    toString(): string;
}

export interface RegExp {
    /** Executes a search for a match in a specified string */
    exec(str: string): any;
    /** Tests for a match in a string */
    test(str: string): boolean;
}

export interface StringConstructor {
    /** Returns a string created from the specified sequence of UTF-16 code units */
    fromCharCode(charCode: number): string;
    /** Returns a string created from the specified sequence of code points */
    fromCodePoint(codePoint: number): string;
}

export interface String {
    /** Returns the character at the specified index */
    charAt(index: number): string;
    /** Returns the UTF-16 code unit at the given index */
    charCodeAt(index: number): number;
    /** Returns a non-negative integer that is the Unicode code point value */
    codePointAt(index: number): number;
    /** Extracts a section of a string and returns it as a new string */
    slice(start: number, end: number): string;
    /** Returns the part of the string between the start and end indexes */
    substring(start: number, end: number): string;
    /** Combines the text of two or more strings and returns a new string */
    concat(str: string): string;
    /** Returns the index of the first occurrence of a specified value */
    indexOf(searchValue: string, fromIndex: number): number;
    /** Returns the index of the last occurrence of a specified value */
    lastIndexOf(searchValue: string, fromIndex: number): number;
    /** Retrieves the result of matching a string against a regular expression */
    match(regexp: RegExp): any;
    /** Returns a new string with some or all matches of a pattern replaced */
    replace(pattern: string|RegExp, replacement: string|Function): string;
    /** Returns a new string with all matches of a pattern replaced */
    replaceAll(pattern: string|RegExp, replacement: string|Function): string;
    /** Executes a search for a match between a regular expression and this string */
    search(regexp: RegExp): number;
    /** Divides a string into an ordered list of substrings */
    split(separator: string|RegExp, limit: number): string[];
    /** Returns the calling string value converted to lowercase */
    toLowerCase(): string;
    /** Returns the calling string value converted to uppercase */
    toUpperCase(): string;
    /** Removes whitespace from both ends of a string */
    trim(): string;
    /** Removes whitespace from the end of a string */
    trimEnd(): string;
    /** Removes whitespace from the beginning of a string */
    trimStart(): string;
    /** Returns a string representing the specified object */
    toString(): string;
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
    /** Parses a string argument and returns an integer */
    function parseInt(string: string, radix: number): number;
    /** Parses a string argument and returns a floating point number */
    function parseFloat(string: string): number;
    /** Evaluates JavaScript code represented as a string */
    function eval(code: string): any;
    /** Determines whether a value is NaN (Not-A-Number) */
    function isNaN(value: any): boolean;
    /** Determines whether a value is a finite number */
    function isFinite(value: any): boolean;
    /** Print values to console output */
    function print(args: any): void;
    /** Triggers garbage collection (development/testing only) */
    function gc(): void;
    /** Loads and executes a JavaScript file */
    function load(filename: string): any;
    /** Loads a memory-mapped bytecode file for efficient execution */
    function loadMapped(filename: string, offset: number, size: number): any;
    function loadUserBytecode(): any;
    /** Executes a function after a specified delay in milliseconds */
    function setTimeout(callback: Function, delay: number): number;
    /** Cancels a timeout previously established by setTimeout */
    function clearTimeout(timeoutId: number): void;

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
    /** Register click handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event). */
    onClick(callback: () => boolean | void): void;
    /** Register long press handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event). */
    onLongPress(callback: () => boolean | void): void;
    /** Register release handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event). */
    onRelease(callback: () => boolean | void): void;
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
    /** Lists (rollers) on the main display, from config.json "lists" */
    readonly list: FbMainList;
    /** Images on the main display, from config.json "images" (present only when config.json has images) */
    readonly image: FbMainImage;
}

export interface FbMainDisplay {
    /** Create a display item; returns its id */
    create(x: number, y: number, width: number, boxType: number, fontSize: number, alignment: number, label: string, unit: string, round: number, page: number): number;
    /** Remove a display item (by id or config.json name) */
    remove(idOrName: number | string): boolean;
    /** Get number of display items */
    count(): number;
    /** Get a display item by id or config.json name */
    get(idOrName: number | string): object;
    /** Get all display items */
    getAll(): Array;
    /** Set item label (item by id or config.json name) */
    setLabel(idOrName: number | string, label: string): void;
    /** Set item value (item by id or config.json name) */
    setValue(idOrName: number | string, value: any): void;
    /** Set item unit (item by id or config.json name) */
    setUnit(idOrName: number | string, unit: string): void;
    /** Set item position (item by id or config.json name) */
    setPosition(idOrName: number | string, x: number, y: number): void;
    /** Set item page, 0 = every page (item by id or config.json name) */
    setPage(idOrName: number | string, page: number): void;
    /** Show a page of the main display: items and the clock on this page or on page 0 (every page) are shown, the others hidden. Page 1 is shown at boot. */
    showPage(page: number): void;
    /** Page of the main display shown now */
    getShownPage(): number;
    /** Show an icon from config.json "icons" before the value ("" hides it; item by id or config.json name); false when there is no such item */
    setIcon(idOrName: number | string, icon: string): boolean;
    /** Set the font of the value and unit (item by id or config.json name; the caption keeps its own; font "" restores config.json); false when there is no such item */
    setFont(idOrName: number | string, font: string | number, size: number): boolean;
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
    /** Label currently shown (last setLabel / MQTT text, else the config label) */
    getLabel(): string;
    /** Top label currently shown (last setTopLabel / MQTT text, else the config label) */
    getTopLabel(): string;
    /** Show an icon from config.json "icons" instead of the bottom label ("" shows the label again) */
    setIcon(icon: string): void;
    /** Set the font of the bottom label (default: the style's key label font, config.json "style.key.label"; font "" restores config.json) */
    setFont(font: string | number, size: number): void;
    /** Set the font of the top label (default: the style's key top label font, config.json "style.key.toplabel"; font "" restores config.json) */
    setTopFont(font: string | number, size: number): void;
}

export interface FbMainList {
    /** Select the next item (one down; stops at the last, no wrap); returns the new index */
    next(nameOrIndex: string | number): number;
    /** Select the previous item (one up; stops at the first, no wrap); returns the new index */
    prev(nameOrIndex: string | number): number;
    /** Select an item by index; false when the index is out of range */
    select(nameOrIndex: string | number, index: number): boolean;
    /** Index of the selected item; -1 when the list is empty */
    getSelected(nameOrIndex: string | number): number;
    /** Number of items in the list */
    count(nameOrIndex: string | number): number;
    /** Replace the items (no newlines in an item); the selection is clamped to the new items */
    setItems(nameOrIndex: string | number, items: string[]): boolean;
    /** Set the font of the items (default: the style's list font, config.json "style.list", at the list's "fontsize"; font "" restores config.json) */
    setFont(nameOrIndex: string | number, font: string | number, size: number): void;
}

export interface FbMainImage {
    /** Show an icon from config.json "icons" at the image's width x height ("" clears the image) */
    setIcon(nameOrIndex: string | number, icon: string): void;
    /** Show any key of the device's images bucket (an LVGL .bin raster, e.g. "radar.bin"; "" clears the image) */
    setSrc(nameOrIndex: string | number, key: string): void;
}

export interface FbRoot {
    /** Key by its "name" in config.json: the same object as fb.main.left/right (FbMainSide) or fb.bars[i].left/right (FbSide). Throws ReferenceError for an unknown name. */
    key(name: string): FbSide | FbMainSide;
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
