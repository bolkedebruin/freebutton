/**
 * Check if code is incomplete (unclosed brackets, strings, etc.)
 * Returns true if code needs more input before execution
 */
export function isIncompleteCode(code: string): boolean {
    let braces = 0;      // {}
    let parens = 0;      // ()
    let brackets = 0;    // []
    let inString: string | null = null;  // Track quote type
    let inTemplate = false;  // Template literal
    let escaped = false;

    for (let i = 0; i < code.length; i++) {
        const char = code[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (char === '\\') {
            escaped = true;
            continue;
        }

        // Handle strings
        if (inString) {
            if (char === inString) {
                inString = null;
            }
            continue;
        }

        if (inTemplate) {
            if (char === '`') {
                inTemplate = false;
            }
            continue;
        }

        // Start of string/template
        if (char === '"' || char === "'") {
            inString = char;
            continue;
        }
        if (char === '`') {
            inTemplate = true;
            continue;
        }

        // Skip single-line comments
        if (char === '/' && code[i + 1] === '/') {
            const newlineIdx = code.indexOf('\n', i);
            if (newlineIdx === -1) break;
            i = newlineIdx;
            continue;
        }

        // Skip multi-line comments
        if (char === '/' && code[i + 1] === '*') {
            const endIdx = code.indexOf('*/', i + 2);
            if (endIdx === -1) return true;  // Unclosed comment
            i = endIdx + 1;
            continue;
        }

        // Count brackets
        switch (char) {
            case '{': braces++; break;
            case '}': braces--; break;
            case '(': parens++; break;
            case ')': parens--; break;
            case '[': brackets++; break;
            case ']': brackets--; break;
        }
    }

    // Incomplete if any unclosed brackets or strings
    if (braces > 0 || parens > 0 || brackets > 0 || inString || inTemplate) {
        return true;
    }

    // Check if line ends with an operator that expects continuation
    const trimmed = code.trim();
    const continuationOperators = ['+', '-', '*', '/', '%', '&', '|', '^', '=', '<', '>', '?', ':', ',', '&&', '||', '??'];

    for (const op of continuationOperators) {
        if (trimmed.endsWith(op)) {
            return true;
        }
    }

    return false;
}
