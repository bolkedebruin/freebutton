/**
 * FreeButton TypeScript Type Definitions
 * Web application internal types for autocomplete and editor features
 */

// ============================================================================
// Re-export types from auto-generated freebutton.d.ts
// ============================================================================
// Runtime API types are auto-generated from C stdlib sources
// Run: npm run generate-types to regenerate

export type {
    LedAPI,
    ButtonAPI,
    SensorAPI,
    MqttAPI,
    ConsoleAPI,
    PerformanceAPI,
    MathConstructor,
    JSONConstructor,
    CompileOptions,
    CompilerVersion,
    MQuickJSCompiler
} from './freebutton.d.ts';

// ============================================================================
// Autocomplete Types (Web App Specific)
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
    [key: string]: APIMethodGroup;  // Allow any API from stdlib-globals.json
}
