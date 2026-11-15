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

## Workflow Integration

`generate-types.js` gives the editor its autocomplete and type checking. The
validator (`src/js/validator.ts`) takes the names of the built-ins from the
generated `src/js/stdlib.json`.

