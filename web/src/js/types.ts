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
    CompileOptions,
    MQuickJSCompiler
} from './freebutton.d.ts';

// ============================================================================
// Autocomplete Types (Web App Specific)
// ============================================================================

interface CompletionItem {
    label: string;
    type: string;
    detail: string;
    info: string;
    apply: string;
}

interface APIMethodGroup {
    methods: CompletionItem[];
}

export interface FreebuttonAPIDefinition {
    [key: string]: APIMethodGroup;  // any API of the stdlib
}
