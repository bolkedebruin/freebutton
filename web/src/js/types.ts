/**
 * FreeButton TypeScript Type Definitions
 * Defines types for the FreeButton standard library APIs
 */

// ============================================================================
// LED API Types
// ============================================================================

export interface LedAPI {
    /** Turn LED white at position */
    on(position: number): void;

    /** Turn LED off at position */
    off(position: number): void;

    /** Set LED RGB color (0-255 for each component) */
    setColor(position: number, r: number, g: number, b: number): void;

    /** Get number of available LEDs */
    count(): number;
}

// ============================================================================
// Button API Types
// ============================================================================

export type ButtonCallback = () => void;

export interface ButtonAPI {
    /** Update button label text */
    setLabel(position: number, text: string): void;

    /** Update button top label text */
    setTopLabel(position: number, text: string): void;

    /** Register click event handler */
    onClick(position: number, callback: ButtonCallback): void;

    /** Register long press event handler */
    onLongPress(position: number, callback: ButtonCallback): void;

    /** Register release event handler */
    onRelease(position: number, callback: ButtonCallback): void;

    /** Get number of available buttons */
    count(): number;
}

// ============================================================================
// Sensor API Types
// ============================================================================

export interface SensorInfo {
    id: number;
    type: string;
    value: number;
    unit?: string;
}

export interface SensorAPI {
    /** Read current sensor value */
    getValue(sensorId: number): number;

    /** Get sensor type ("temperature", "humidity", etc.) */
    getType(sensorId: number): string;

    /** Get sensor information object */
    getInfo(sensorId: number): SensorInfo;

    /** Get array of all sensor information */
    getAll(): SensorInfo[];

    /** Get number of available sensors */
    count(): number;
}

// ============================================================================
// MQTT API Types
// ============================================================================

export type MqttMessageCallback = (topic: string, message: string) => void;
export type MqttConnectionCallback = () => void;

export interface MqttAPI {
    /** Publish message to MQTT topic */
    publish(
        brokerId: number,
        topic: string,
        payload: string,
        qos?: number,
        retain?: boolean
    ): void;

    /** Subscribe to MQTT topic */
    subscribe(
        brokerId: number,
        topic: string,
        callback: MqttMessageCallback,
        qos?: number
    ): void;

    /** Unsubscribe from MQTT topic */
    unsubscribe(brokerId: number, topic: string): void;

    /** Register broker connect callback */
    onConnect(brokerId: number, callback: MqttConnectionCallback): void;

    /** Register broker disconnect callback */
    onDisconnect(brokerId: number, callback: MqttConnectionCallback): void;

    /** Check broker connection status */
    isConnected(brokerId: number): boolean;

    /** Get broker name */
    getBrokerName(brokerId: number): string;

    /** Get number of configured brokers */
    getBrokerCount(): number;
}

// ============================================================================
// Global FreeButton API
// ============================================================================
// Note: Global declarations are auto-generated in freebutton.d.ts
// by scripts/generate-types.js from C stdlib sources

// ============================================================================
// Compiler API Types
// ============================================================================

export interface CompileOptions {
    /** Flash address for pre-relocation (default: 0) */
    targetAddr?: number;

    /** Generate 32-bit bytecode (default: true for ESP32) */
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

// Note: mqjsCompiler global declaration is in freebutton.d.ts

// ============================================================================
// Autocomplete Types
// ============================================================================

export interface CompletionItem {
    label: string;
    type: string;
    detail: string;
    info: string;
    apply: string;
}

export interface APIMethodGroup {
    methods: CompletionItem[];
}

export interface FreebuttonAPIDefinition {
    led: APIMethodGroup;
    button: APIMethodGroup;
    sensor: APIMethodGroup;
    mqtt: APIMethodGroup;
}

// Export empty object to make this a module
export {};
