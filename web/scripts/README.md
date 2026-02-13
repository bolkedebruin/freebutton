# Scripts

This directory contains build and code generation scripts for the FreeButton Web Editor.

## generate-types.js

Auto-generates TypeScript type definitions from FreeButton C stdlib source files.

### Usage

```bash
npm run generate-types
```

### What it does

1. Parses C source files in `../src/scripting/`:
   - `freebutton_led.c`
   - `freebutton_button.c`
   - `freebutton_sensor.c`
   - `freebutton_mqtt.c`

2. Extracts JavaScript API definitions from comment blocks:
   ```c
   /*
    * JavaScript API:
    *   led.on(position) - Turn LED white at position
    */
   ```

3. Generates TypeScript interfaces in `src/js/freebutton.d.ts`:
   ```typescript
   interface LedAPI {
       on(position: number): void;
   }
   ```

### When to run

Run this script whenever the FreeButton stdlib APIs are updated to keep TypeScript types in sync with the C implementation.

---

## extract-stdlib-globals.js

Extracts all JavaScript globals from MQuickJS and FreeButton stdlib for runtime validation.

### Usage

```bash
npm run extract-globals
```

**Note:** This runs automatically before each build via the `prebuild` npm hook.

### What it does

1. Parses MQuickJS stdlib (`../lib/mquickjs/mqjs_stdlib.c`):
   - Extracts all class definitions (Object, Array, String, Math, etc.)
   - Identifies typed arrays (Uint8Array, Int32Array, etc.)
   - Finds global constructors and objects

2. Parses FreeButton stdlib (`../lib/mquickjs/freebutton_stdlib.c`):
   - Extracts API objects (led, button, sensor, mqtt)
   - Maps object definitions to runtime instances

3. Parses TypeScript declarations (`src/js/freebutton.d.ts`):
   - Extracts global functions (print, setTimeout, etc.)
   - Extracts API method signatures

4. Generates `src/js/stdlib-globals.json` containing:
   ```json
   {
     "globals": ["Array", "Object", "led", "print", ...],
     "apis": {
       "led": ["on", "off", "setColor", "count"],
       "button": ["setLabel", "onClick", ...],
       ...
     }
   }
   ```

### How it's used

The validator (`src/js/validator.ts`) imports this JSON file to:
- Check if function calls reference known globals
- Validate API method calls (e.g., `led.setColor` vs `led.invalidMethod`)
- Provide intelligent suggestions for typos
- Track which identifiers are stdlib vs user-defined

### When to run

Run this script (or rebuild) whenever:
- MQuickJS stdlib is updated
- FreeButton API methods are added/changed
- Global functions are added to the runtime

---

## Workflow Integration

Both scripts work together to provide type safety and runtime validation:

1. **Development**: `generate-types.js` provides IDE autocomplete and type checking
2. **Build**: `extract-globals.js` generates validation data (runs automatically)
3. **Runtime**: Validator uses stdlib-globals.json to catch errors before compilation

This ensures the web editor stays synchronized with the actual firmware capabilities.
