# FreeButton Web Editor

Browser-based JavaScript IDE for FreeButton development with CodeMirror 6, featuring syntax highlighting, auto-completion, and bytecode compilation.

## Features

- **Modern Code Editor**: CodeMirror 6 with JavaScript syntax highlighting
- **Dark Theme**: Professional One Dark theme
- **MQuickJS Strict Validation**: Comprehensive ES5 strict mode enforcement
  - **Acorn-based AST parser** with custom MQuickJS plugin
  - **Stdlib-aware validation**: Checks against actual MQuickJS + FreeButton stdlib (auto-generated from C sources)
  - **API method validation**: Verifies led.*, button.*, sensor.*, mqtt.* method calls
  - **Undefined identifier detection**: Catches typos with intelligent suggestions (e.g., `setTimeou` → `setTimeout`)
  - **Stricter mode checks**: No `with`, no direct `eval()`, no array holes
  - **Value boxing detection**: Prevents `new Number()`, `new String()`, `new Boolean()`
  - **ES6+ feature blocking**: Rejects arrow functions, classes, template literals, destructuring
  - **Array boundary warnings**: Flags non-sequential array index assignments
  - **Scope tracking**: Validates variable declarations across function scopes
  - **Precise error reporting**: Line and column numbers for all violations
- **WASM Compiler**: Compile JavaScript to FreeButton bytecode in the browser
- **IntelliSense Autocomplete**: Context-aware code completion for FreeButton APIs
  - Type `led.` to see LED methods (on, off, setColor, count)
  - Type `button.` to see button methods (onClick, setLabel, etc.)
  - Type `sensor.` to see sensor methods (getValue, getType, etc.)
  - Type `mqtt.` to see MQTT methods (publish, subscribe, etc.)
  - Includes function signatures and documentation tooltips
- **Example Scripts**: Pre-loaded examples for LED, MQTT, sensors, and buttons
- **Bytecode Download**: Generate and download `.jsc` files ready for deployment

## Quick Start

### Initial Setup

```bash
cd web
npm install
```

### Development

```bash
# Start development server with watch mode
npm run dev

# In another terminal, serve the built files
npm run serve

# Then open http://localhost:8080
```

### Production Build

```bash
# Build optimized bundle
npm run build

# Preview production build
npm run preview
```

## Project Structure

```
web/
├── src/                      # Source files (TypeScript)
│   ├── index.html           # Main IDE page
│   ├── js/
│   │   ├── main.ts          # Application entry point
│   │   ├── editor.ts        # CodeMirror setup and API
│   │   ├── compiler.ts      # WASM compiler integration
│   │   ├── ui.ts            # UI controls and interactions
│   │   ├── autocomplete.ts  # FreeButton API autocomplete
│   │   └── types.ts         # TypeScript type definitions
│   └── css/
│       └── styles.css       # Custom styles
├── dist/                     # Built files (generated, gitignored)
│   ├── index.html
│   ├── bundle.js            # Bundled JavaScript
│   ├── bundle.js.gz         # Gzipped bundle
│   └── assets/              # WASM compiler files
├── tsconfig.json            # TypeScript configuration
├── package.json             # Dependencies and scripts
├── rollup.config.js         # Build configuration
└── README.md                # This file
```

## TypeScript

The project is written in TypeScript for type safety and better developer experience:
- **Strict type checking** enabled for maximum safety
- **Auto-generated type definitions** from C stdlib source
- **IntelliSense support** in IDEs (VS Code, WebStorm, etc.)
- **Type-safe autocomplete** definitions
- **Source maps** for debugging

### Generate Types and Globals from Stdlib

TypeScript definitions and validation data are auto-generated from the C source files:

```bash
# Generate TypeScript type definitions
npm run generate-types
```

**Type Generation** parses JavaScript API comments in C files and generates `src/js/freebutton.d.ts`.

Run it whenever the stdlib is updated; `npm run build` does so by itself (the `prebuild` hook).

## Build System

The project uses Rollup with TypeScript to bundle CodeMirror 6 and application code into a single optimized JavaScript file.

### Build Process

1. **Bundle**: Combines all JavaScript modules using Rollup
2. **Copy Assets**: Copies HTML, CSS, and WASM files to `dist/`
3. **Minify**: Reduces bundle size with Terser (production only)
4. **Gzip**: Creates compressed versions for faster delivery (production only)
5. **Source Maps**: Generates debugging maps (development only)

### NPM Scripts

- `npm run dev` - Development build with watch mode (rebuilds on file changes)
- `npm run build` - Production build (minified + gzipped)
- `npm run serve` - Serve `dist/` on http://localhost:8080
- `npm run preview` - Build and serve production bundle

## Dependencies

### Runtime Dependencies
- `codemirror@^6.0.1` - Core editor framework
- `@codemirror/lang-javascript@^6.2.1` - JavaScript language support
- `@codemirror/theme-one-dark@^6.1.2` - Dark theme
- `acorn@^8.0.0` - ES5 JavaScript parser for validation
- `@codemirror/lang-json` - JSON mode for editing `config.json`
- `@aws-sdk/client-s3` - S3 client for the device file storage (only the
  ListObjectsV2/Get/Put/Delete/Head commands are bundled, about 68 KB gzipped)

## Device Files (App, Images and Fonts panels)

The device serves LittleFS as a minimal S3-compatible API with path-style
URLs and one bucket per kind of content. Each bucket is a directory on the
device, and an object key is the path relative to that directory:

| Bucket | Directory on the device | Access | Example key |
|---|---|---|---|
| `app` | `/`, without the directories of the other buckets | read/write | `config.json`, `scripts/main.js` |
| `examples` | `/scripts/examples` | read-only | `button.js` |
| `web` | `/www` (the IDE bundle, gzipped) | read-only | `index.html.gz` |
| `fonts` | `/fonts` (the device fonts) | read-only | `mnt_medium_14.bin` |

Each file is in exactly one bucket: `app` does not contain `www/`, `fonts/` or
`scripts/examples/`. A write to a read-only bucket gets 403 AccessDenied.
`src/js/device-storage.ts` is the only module that imports the AWS SDK. It uses
dummy credentials and a no-op signer, because the device ignores signatures,
and it does no retries. `src/js/workspace.ts` owns the file list and the file
that is open in the editor.

- **Boot script**: the editor opens `/scripts/main.js` at start. If that file
  is not on the device yet, the editor starts from a template, and the first
  Save creates the file.
- **Save** (toolbar or Ctrl/Cmd+S): stores the source in bucket `app`. Before
  the request, the IDE checks the same rules as the device: the extension must
  be `.js`, `.json` or `.txt`; the file must not be under `www/`, `fonts/` or
  `scripts/examples/` (those are the read-only buckets); the
  key can be at most 63 bytes and the body at most 64 KiB. `.json` files must be
  valid JSON. For `config.json` the IDE also checks the structure, then offers
  a reboot, because the device applies the config at boot.
- **Deploy** (toolbar or Ctrl/Cmd+Enter), for `scripts/main.js` only: saves the
  file, checks it, compiles it for the device and uploads the bytecode as
  `main.jsc`, which is the file the firmware runs at boot. The button shows the
  progress and the result. With another file open, Deploy is disabled and the
  toolbar says why. There is no separate Compile or Validate step: the editor
  checks the file as you type (click the error count in the status bar to go
  to the first error), and Deploy refuses a script with errors.
- **More** ("⋯" in the toolbar): Download bytecode (.jsc) compiles the open
  script and saves the bytecode on your computer; Clear file text asks first.
- **Left bar**: one selection decides the whole middle pane, the main view
  and the list that belongs to it. Device and Docs have no list.
- **App**, **Images**, **Fonts** (left bar): one sidebar panel each. *App*
  is the editor with its file list (there is no separate Code entry); it
  lists the files of bucket `app`: `.js`, `.json` and `.txt` open in the
  editor; files the IDE cannot open are under "Other" and you can delete them
  (firmware before 2026-11-13 refuses a delete of other extensions). You cannot
  delete `config.json`. *Images* lists `icons/*.svg`; an image opens in a view
  that shows it at several sizes. *Fonts* lists the fonts on the device and
  shows the Fonts view. The IDE requests the lists when it starts, when you
  click refresh, after a save, create or delete, and when the REPL connects to
  a different device. It never polls.
- **Used and free space**: the bottom line of the App, Images and Fonts panels.
  App and Images are files of the device's filesystem; fonts are in its media
  store. The numbers are the `x-fb-fs-*` and `x-fb-media-*` headers the device
  puts on its S3 listings, so they cost no request (firmware without them: the
  App panel shows what its files add up to).
- **Update** (Device view): "Update firmware…" installs a `firmware.bin`; your
  files stay. "Update filesystem…" installs a `littlefs.bin` (`pio run -t
  buildfs`); it replaces every file on the device (config.json, scripts, the
  IDE) and asks first. Both show the upload percentage, then "applying" while
  the device restarts, then the result. To update the IDE and keep the files,
  use `tools/scripts/ota_fs.py`.
- **Drag and drop**: with Images or Fonts selected, drop `.svg`, `.ttf` or
  `.otf` files anywhere on the middle pane to upload them. Each file goes where
  its type belongs; other files are named in the console and not uploaded.
- **Push images** (toolbar, only with the Images panel or an image open):
  makes the rasters the device draws from the SVGs that `config.json` uses and
  uploads them. The IDE also does this on every save of `config.json` and after
  an SVG upload or delete, so the button is for a retry after a failure.
- **Docs** (left bar): the keyboard shortcuts. The documentation is not linked
  yet.
- **Fonts view**: every font of bucket `fonts` draws the same sample text (you
  can change it) at one of the device's sizes, so that you can compare them.
  Each font has "Add to config.json" and Delete (not the system fonts).
- **Examples** (left bar): bundled into the IDE at build time from
  `data/scripts/examples`. The main pane shows the chosen example read-only;
  looking at one makes no file and changes none.
- **Device view** (Device in the left bar): status, free memory, storage use,
  Reboot, and Screenshot. Screenshot shows what the displays show now, and
  "Save as PNG" stores one on your computer. The device has to write the
  frames to its storage for a moment (a frame does not fit its RAM); the IDE
  downloads them and deletes them from the device at once
  (`src/js/screenshot.ts`).
- **Virtual device**: the key labels and top labels (`toplabel`, or `topLabel`
  in older files) come from the device's `config.json`. The connector layout
  comes from `info.connectors`. With `config.json` open, the entry under the
  cursor (key, display item, list or image) has an outline on the virtual
  device, and a click on an item of the virtual device selects its entry in
  `config.json` (`src/js/config-locate.ts`). The "i" button shows read-only
  device info on the back.
- **No device** (dev without hardware, or firmware without the S3 API): files
  are kept in browser memory only, and the App panel says so. Firmware with
  the older single bucket `fs` answers NoSuchBucket for `app`; the App panel
  then says that the device firmware must be updated.

### Build Dependencies
- `rollup@^4.0.0` - Module bundler
- `@rollup/plugin-node-resolve@^15.0.0` - Resolve node_modules
- `@rollup/plugin-commonjs@^25.0.0` - Convert CommonJS to ES6
- `@rollup/plugin-terser@^0.4.0` - Minification
- `@rollup/plugin-typescript@^12.0.0` - TypeScript compilation
- `typescript@^5.0.0` - TypeScript compiler
- `@types/acorn@^4.0.0` - Acorn type definitions
- `rollup-plugin-copy@^3.5.0` - Copy static assets
- `rollup-plugin-gzip@^3.1.0` - Gzip compression

## MQuickJS Validation

The editor enforces **MQuickJS stricter mode** - a strict subset of ES5 JavaScript optimized for embedded systems. The validator catches violations before compilation.

### Forbidden Constructs

**Strict Mode Violations:**
```javascript
// FORBIDDEN: with statement
with (obj) { x = 1; }  // Error: forbidden in strict mode

// FORBIDDEN: Direct eval
eval('1 + 2');  // Error: Use indirect eval instead

// ALLOWED: Indirect (global) eval
(1, eval)('1 + 2');  // OK
```

**Value Boxing:**
```javascript
// FORBIDDEN: Value boxing constructors
var n = new Number(42);      // Error
var s = new String("hello"); // Error
var b = new Boolean(true);   // Error

// ALLOWED: Direct conversion
var n = Number(42);      // OK
var s = String("hello"); // OK
var b = Boolean(true);   // OK
```

**Array Holes:**
```javascript
// FORBIDDEN: Arrays with holes
var arr = [1, , 3];  // Syntax error

// ALLOWED: Explicit undefined
var arr = [1, undefined, 3];  // OK
```

**Array Boundaries:**
```javascript
// ALLOWED: Sequential extension
var arr = [];
arr[0] = 1;  // OK
arr[1] = 2;  // OK

// FORBIDDEN: Non-sequential assignment
arr[10] = 3;  // Runtime TypeError in MQuickJS
```

**ES6+ Features:**
```javascript
// FORBIDDEN: Arrow functions
const fn = () => {};  // Error: Use function expressions

// FORBIDDEN: Classes
class Foo {}  // Error: Use constructor functions

// FORBIDDEN: Template literals
const s = `hello ${name}`;  // Error: Use string concatenation

// FORBIDDEN: Destructuring
const {x, y} = obj;  // Error: Use direct access

// FORBIDDEN: Spread/Rest
const arr = [...other];  // Error
function fn(...args) {}  // Error
```

### Allowed ES5+ Extensions

MQuickJS supports limited modern features:

```javascript
// Exponentiation operator
var squared = x ** 2;

// for...of with arrays only
for (var item of [1, 2, 3]) {
    print(item);
}

// Typed arrays
var buffer = new Uint8Array(10);

// Modern string methods
var s = "hello";
s.trimStart();
s.trimEnd();
s.codePointAt(0);

// Modern Math functions
Math.imul(a, b);
Math.clz32(x);
Math.fround(f);

// globalThis
globalThis.myVar = 123;
```

## FreeButton API

The editor supports auto-completion for FreeButton JavaScript APIs:

- `led.*` - LED control (setColor, setBrightness, etc.)
- `button.*` - Button state and events
- `sensor.*` - Environmental sensor readings
- `mqtt.*` - MQTT publish/subscribe

## WASM Compiler

The JavaScript to bytecode compiler is built from the `../lib/mquickjs/` directory:

```bash
cd ../lib/mquickjs
./build_wasm_mquickjs.sh
```

This generates:
- `mquickjs-compiler.wasm` - WebAssembly compiler module (~245KB)
- `mquickjs-compiler.js` - Emscripten JavaScript wrapper

These files are automatically copied to `dist/assets/` during the build process.

## Browser Compatibility

- Chrome/Edge 90+
- Firefox 88+
- Safari 14+
- Mobile browsers with ES6+ support

## Deployment Options

### Option 1: Static Hosting
Deploy the `dist/` folder to any static hosting service:
- GitHub Pages
- Netlify
- Vercel
- Cloudflare Pages

### Option 2: ESP32 Integration
Serve from the ESP32's LittleFS filesystem (if space permits).

### Option 3: Hybrid
Host static assets on CDN, ESP32 serves API only.

## Development Tips

### Debugging
- Use browser DevTools with source maps in development mode
- Console logs show compilation errors and warnings
- Network tab shows WASM module loading

### Hot Reload
Run `npm run dev` to automatically rebuild on file changes. Refresh browser to see updates.

### Bundle Size Analysis
```bash
npm install --save-dev rollup-plugin-visualizer
# Add to rollup.config.js and run build to see bundle composition
```

## License

GNU General Public License, version 3 (see `LICENSE` in the repository root).
