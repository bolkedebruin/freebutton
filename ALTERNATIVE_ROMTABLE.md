isnt an alternative to store the index in the atom rather than the pointer? or doesnt that make sense?

⏺ That's an excellent question! Yes, storing the ROM index directly in the bytecode instead of the pointer would be cleaner and eliminate the translation table entirely.

Current Approach (Translation Table)

// Bytecode contains WASM ROM pointer
[bytecode]: 0x00002449  // WASM pointer to "setColor"

// Translation table maps: offset → index
[ROM table]: { offset: 0x128, rom_index: 42 }

// Runtime lookup:
offset → rom_index → ESP32 ROM pointer

Alternative: Store Index Directly

// Bytecode contains ROM index directly
[bytecode]: 42  // Index of "setColor" in ROM table

// Runtime lookup (no table needed!):
rom_index → ESP32 ROM pointer

Why This Would Be Better

1. No translation table - Saves 72 bytes in flash
2. Simpler runtime - Direct array lookup instead of binary search
3. True portability - Indices are universal across all devices
4. Cleaner design - No need for v0x0002 format

The Challenge: JSAtom Format

The issue is how QuickJS represents atoms in bytecode:

typedef uint32_t JSAtom;

// JSAtom can be:
// - Small integer (tagged)
// - Atom index (for interned strings)
// - ROM atom POINTER (for stdlib)

QuickJS currently stores ROM atoms as pointers, not indices. The bytecode format expects this:

// In bytecode instruction (e.g., OP_get_var)
{
JSAtom atom = read_atom_from_bytecode();  // Expects pointer or tagged index
JSAtomStruct *p = JS_ATOM_TO_PTR(atom);   // Assumes it's a pointer
}

Possible Solutions

Option 1: Use a Special Tag

Store the ROM index with a special bit pattern that distinguishes it from pointers:

// When WASM compiler detects ROM atom:
uint32_t encode_rom_index(uint16_t rom_idx) {
// Use high bit to mark as ROM index
return 0x80000000 | rom_idx;  // Indices limited to 0-65535
}

// At ESP32 runtime:
uint32_t bc_reloc_value(uint32_t value, uint32_t base_addr) {
if (value & 0x80000000) {
// It's a ROM index!
uint16_t rom_idx = value & 0xFFFF;
return (uint32_t)&esp32_rom_atoms[rom_idx];
}
// Standard relocation
return base_addr + value;
}

Option 2: Modify QuickJS Atom Encoding

This would be more invasive but cleaner - change how QuickJS encodes atoms to support index-based ROM atoms natively. This would require changes to quickjs.c core.

Trade-offs

Translation Table (current):
- ✅ No changes to QuickJS atom encoding
- ✅ Works with existing bytecode format
- ❌ Extra 72 bytes per script
- ❌ Binary search overhead

Direct Index Storage:
- ✅ No extra memory
- ✅ Faster runtime lookup
- ✅ Cleaner design
- ❌ Requires bytecode format changes
- ❌ Need to distinguish indices from pointers
- ❌ More invasive to QuickJS core

For Your Explanation to Bellard

You could present both approaches and ask which he prefers:

"I see two possible approaches:

Approach A (current): Add a ROM translation table that maps bytecode offsets to ROM indices. This keeps the bytecode format unchanged but adds memory overhead.

Approach B: Modify the WASM compiler to store ROM atom indices directly in bytecode (with a tag to distinguish from pointers). This eliminates the table but requires changes to atom encoding.

Which approach would you prefer for upstream mquickjs?"

The direct index approach is architecturally cleaner, but the translation table approach is less invasive to the core QuickJS code. What do you think?

> what would option B look like?

⏺ Let me show you what Option B (direct ROM index storage) would look like with detailed code:

Option B: Direct ROM Index Encoding

Encoding Scheme

// Constants for ROM index encoding
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFF0000
#define JS_ATOM_ROM_INDEX_MASK   0x0000FFFF

// Check if a value is a ROM index (not a pointer)
static inline int is_rom_index(uint32_t value) {
return (value & 0xFFFF0000) == JS_ATOM_ROM_INDEX_MARKER;
}

// Encode ROM index into bytecode
static inline uint32_t encode_rom_index(uint16_t rom_idx) {
return JS_ATOM_ROM_INDEX_MARKER | rom_idx;
}

// Extract ROM index from encoded value
static inline uint16_t decode_rom_index(uint32_t value) {
return value & JS_ATOM_ROM_INDEX_MASK;
}

WASM Compiler Changes (emscripten_wrapper.c)

// In compile_js_to_bytecode() - REPLACE translation table building

// Scan bytecode and convert ROM pointers to indices
uint8_t* scan_and_encode_rom_indices(uint8_t* bc_buf, size_t bc_size,
void* rom_table, size_t rom_count) {
uint32_t* data = (uint32_t*)bc_buf;
size_t word_count = bc_size / 4;

      // Get ROM table bounds (WASM memory addresses)
      uintptr_t rom_start = (uintptr_t)rom_table;
      uintptr_t rom_end = rom_start + (rom_count * sizeof(JSAtomStruct));

      printf("[WASM] Scanning bytecode for ROM atoms (ROM range: 0x%lx - 0x%lx)\n",
             rom_start, rom_end);

      int converted_count = 0;

      // Scan all 32-bit values in bytecode
      for (size_t i = 0; i < word_count; i++) {
          uint32_t value = data[i];

          // Check if this looks like a ROM pointer (in WASM ROM table range)
          if (value >= rom_start && value < rom_end) {
              // Calculate ROM index
              size_t offset = value - rom_start;
              if (offset % sizeof(JSAtomStruct) == 0) {
                  uint16_t rom_idx = offset / sizeof(JSAtomStruct);

                  // Replace pointer with encoded index
                  data[i] = encode_rom_index(rom_idx);
                  converted_count++;

                  printf("[WASM]   Offset 0x%04zx: ROM pointer 0x%08x → index %u\n",
                         i * 4, value, rom_idx);
              }
          }
      }

      printf("[WASM] Converted %d ROM pointers to indices\n", converted_count);
      return bc_buf;
}

// Modified compile function
EM_JS(int, compile_js_to_bytecode, (const char* js_code, uint8_t** out_buf, size_t* out_len), {
// ... existing compilation code ...

      // Get bytecode buffer
      const bcBuf = Module.HEAPU8.subarray(bcPtr, bcPtr + bcSize);
      let bytecode = new Uint8Array(bcBuf);

      // NO TRANSLATION TABLE - just header + data
      // Header stays at 20 bytes (can keep version 0x0001)

      // Convert ROM pointers to indices IN PLACE
      const romTablePtr = _JS_GetStdlibAtomTable();
      const romTableSize = _JS_GetRomAtomTableSize();

      // This modifies bytecode in place
      _scan_and_encode_rom_indices(bcPtr, bcSize, romTablePtr, romTableSize);

      // Now relocate with standard relocation
      const target = 0x3c000000; // Dummy target for initial relocation
      _JS_RelocateBytecode2(bcPtr, bcSize, target, 1); // update_atoms=1

      // Return modified bytecode (no ROM table needed!)
      const finalSize = headerSize + bcSize;
      const finalBuf = Module._malloc(finalSize);

      Module.HEAPU8.set(Module.HEAPU8.subarray(bcPtr, bcPtr + headerSize), finalBuf);
      Module.HEAPU8.set(bytecode, finalBuf + headerSize);

      Module.setValue(out_buf, finalBuf, '*');
      Module.setValue(out_len, finalSize, 'i32');

      return 0;
});

ESP32 Relocation Changes (mquickjs.c)

// Modified bc_reloc_value - check for ROM indices BEFORE relocating
static uint32_t bc_reloc_value(uint32_t value, uint32_t base_addr) {
// Check if this is an encoded ROM index
if (is_rom_index(value)) {
uint16_t rom_idx = decode_rom_index(value);

          // Get ESP32 ROM atom pointer
          extern JSAtomStruct* js_rom_atoms[];  // ESP32's ROM table
          uint32_t rom_ptr = (uint32_t)&js_rom_atoms[rom_idx];

          // Return ESP32 ROM pointer directly
          return rom_ptr;
      }

      // Standard relocation for non-ROM values
      if (value == 0)
          return 0;
      return base_addr + value;
}

// JS_RelocateBytecode2 stays the same - it calls bc_reloc_value
// No changes needed!

// JS_RelocateHdr stays the same
// No changes needed!

// JS_LoadBytecode simplified - no ROM table handling
JSValue JS_LoadBytecode(JSContext *ctx, const uint8_t *buf, size_t buf_len) {
const JSBytecodeHeader *hdr = (const JSBytecodeHeader *)buf;

      // Version check (0x0001 or 0x8001)
      if (hdr->version != JS_BYTECODE_VERSION_32_V1 &&
          hdr->version != JS_BYTECODE_VERSION_64_V1) {
          return JS_ThrowSyntaxError(ctx, "invalid bytecode version");
      }

      // Verify relocation
      uint32_t expected_base = (uint32_t)(hdr + 1);  // No ROM table offset!
      if (hdr->base_addr != expected_base) {
          return JS_ThrowSyntaxError(ctx, "bytecode not relocated");
      }

      // Load and execute
      // ROM indices are automatically translated by bc_reloc_value during parsing
      return load_and_execute(ctx, hdr);
}

ESP32 BytecodeWriter Changes (BytecodeWriter.cpp)

// MASSIVELY SIMPLIFIED - no ROM table handling at all!

void BytecodeWriter::writeBytecode(const uint8_t* data, size_t length) {
// 1. Write header
JSBytecodeHeader* hdr = (JSBytecodeHeader*)data;
uint32_t target_base = flash_addr + sizeof(JSBytecodeHeader);

      JS_RelocateHdr(hdr, target_base);
      partition.write(flash_addr, hdr, sizeof(JSBytecodeHeader));

      ESP_LOGI(TAG, "Header relocated: base_addr=0x%08x", hdr->base_addr);

      // 2. Relocate and write bytecode in chunks
      uint32_t offset = sizeof(JSBytecodeHeader);
      const size_t CHUNK_SIZE = 4096;

      while (offset < length) {
          uint32_t chunk_size = (length - offset > CHUNK_SIZE) ? CHUNK_SIZE : (length - offset);

          // Relocate chunk (ROM indices are handled automatically!)
          JS_RelocateMtag((uint8_t*)data + offset, chunk_size, target_base);

          // Write to flash
          partition.write(flash_addr + offset, data + offset, chunk_size);
          offset += chunk_size;
      }

      ESP_LOGI(TAG, "Bytecode written: %u bytes", length);
}

Bytecode Format Comparison

Before (with translation table):
[Header - 20 bytes]
version: 0x0002
base_addr: 0x3c31c05c
rom_atom_count: 9

[ROM Translation Table - 72 bytes]
{ offset: 0x128, rom_index: 42 }
{ offset: 0x134, rom_index: 87 }
...

[Bytecode - 404 bytes]
0x128: 0x00002449  ← WASM ROM pointer (needs translation)

After (direct indices):
[Header - 20 bytes]
version: 0x0001  ← Can stay v1!
base_addr: 0x3c31c048

[Bytecode - 404 bytes]
0x128: 0xFFFF002A  ← Encoded ROM index (42)
0x134: 0xFFFF0057  ← Encoded ROM index (87)

Key Advantages

1. No ROM table - Saves 72 bytes (or more for larger scripts)
2. Simpler code - BytecodeWriter is much simpler
3. Stays v0x0001 - No new version needed
4. Faster runtime - Direct array lookup vs binary search
5. Universal - ROM indices work across all devices

Potential Issues

1. Marker collision - If bytecode legitimately contains 0xFFFF0000+ values
   - Very unlikely for pre-relocation offsets (would need >4GB bytecode)
2. Max ROM atoms - Limited to 65,535 atoms
   - Current QuickJS has ~300 ROM atoms, so plenty of room
3. Debugging - ROM indices are less readable than pointers
   - But same as translation table approach

Would you prefer this approach? It's cleaner architecturally but requires scanning the entire bytecode in WASM to convert pointers → indices.

> would this work for 64bit?

⏺ Excellent question! The encoding scheme needs adjustment for 64-bit, but it can absolutely work. Let me show you how:

64-bit Considerations

The Problem

In 64-bit builds:
- Pointers are 64-bit (8 bytes)
- base_addr in the header is 64-bit
- ROM pointers in bytecode are 64-bit
- But we're using a 32-bit marker (0xFFFF0000)

Solution: Conditional Encoding

#if UINTPTR_MAX == 0xFFFFFFFF
// 32-bit architecture
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFF0000UL
#define JS_ATOM_ROM_INDEX_MASK   0x0000FFFFUL
typedef uint32_t bc_atom_t;
#else
// 64-bit architecture
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFFFFFFFFFF0000ULL
#define JS_ATOM_ROM_INDEX_MASK   0x000000000000FFFFULL
typedef uint64_t bc_atom_t;
#endif

// Encoding works the same way on both architectures
static inline int is_rom_index(bc_atom_t value) {
return (value & ~JS_ATOM_ROM_INDEX_MASK) == JS_ATOM_ROM_INDEX_MARKER;
}

static inline bc_atom_t encode_rom_index(uint16_t rom_idx) {
return JS_ATOM_ROM_INDEX_MARKER | rom_idx;
}

static inline uint16_t decode_rom_index(bc_atom_t value) {
return (uint16_t)(value & JS_ATOM_ROM_INDEX_MASK);
}

Why This Works

32-bit:
- Marker: 0xFFFF0000 (impossible pre-relocation offset)
- Index: 16 bits (0-65535 atoms)
- Example: 0xFFFF002A = ROM index 42

64-bit:
- Marker: 0xFFFFFFFFFFFF0000 (impossible address - high bits unused)
- Index: 16 bits (0-65535 atoms)
- Example: 0xFFFFFFFFFFFF002A = ROM index 42

Real 64-bit addresses on x86-64 only use 48 bits (canonical form), so values with high 16 bits all set are invalid and perfect for our marker.

Complete Cross-Architecture Implementation

// mquickjs.h - Architecture-agnostic definitions

#if UINTPTR_MAX == 0xFFFFFFFF
// 32-bit
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFF0000UL
#define JS_ATOM_ROM_INDEX_MASK   0x0000FFFFUL
typedef uint32_t bc_atom_t;

      #define BC_ATOM_FMT "0x%08x"
      #define JS_BYTECODE_VERSION JS_BYTECODE_VERSION_32_V1
#else
// 64-bit
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFFFFFFFFFF0000ULL
#define JS_ATOM_ROM_INDEX_MASK   0x000000000000FFFFULL
typedef uint64_t bc_atom_t;

      #define BC_ATOM_FMT "0x%016llx"
      #define JS_BYTECODE_VERSION JS_BYTECODE_VERSION_64_V1
#endif

// These functions work on both architectures
static inline int is_rom_index(bc_atom_t value) {
return (value & ~JS_ATOM_ROM_INDEX_MASK) == JS_ATOM_ROM_INDEX_MARKER;
}

static inline bc_atom_t encode_rom_index(uint16_t rom_idx) {
return JS_ATOM_ROM_INDEX_MARKER | (bc_atom_t)rom_idx;
}

static inline uint16_t decode_rom_index(bc_atom_t value) {
return (uint16_t)(value & JS_ATOM_ROM_INDEX_MASK);
}

Relocation Code (Works on Both)

// mquickjs.c - bc_reloc_value now handles both 32 and 64 bit

#if UINTPTR_MAX == 0xFFFFFFFF
// 32-bit relocation
static uint32_t bc_reloc_value(uint32_t value, uint32_t base_addr) {
// Check for ROM index marker
if (is_rom_index(value)) {
uint16_t rom_idx = decode_rom_index(value);
extern JSAtomStruct* js_rom_atoms[];
return (uint32_t)&js_rom_atoms[rom_idx];
}

          // Standard relocation
          if (value == 0) return 0;
          return base_addr + value;
      }
#else
// 64-bit relocation
static uint64_t bc_reloc_value(uint64_t value, uint64_t base_addr) {
// Check for ROM index marker
if (is_rom_index(value)) {
uint16_t rom_idx = decode_rom_index(value);
extern JSAtomStruct* js_rom_atoms[];
return (uint64_t)&js_rom_atoms[rom_idx];
}

          // Standard relocation
          if (value == 0) return 0;
          return base_addr + value;
      }
#endif

WASM Compiler (Works for Target Architecture)

// emscripten_wrapper.c - detects architecture from bytecode version

EM_JS(int, compile_js_to_bytecode, (const char* js_code, uint8_t** out_buf, size_t* out_len), {
// ... compilation ...

      // Get bytecode header
      const hdr = new DataView(Module.HEAPU8.buffer, bcPtr, 20);
      const version = hdr.getUint16(2, true);
      const is64bit = (version & 0x8000) !== 0;  // High bit indicates 64-bit

      console.log(`[WASM] Compiling for ${is64bit ? '64-bit' : '32-bit'} architecture`);

      // Scan and encode ROM indices
      const romTablePtr = _JS_GetStdlibAtomTable();
      const romTableSize = _JS_GetRomAtomTableSize();

      if (is64bit) {
          // Scan 64-bit values
          _scan_and_encode_rom_indices_64(bcPtr, bcSize, romTablePtr, romTableSize);
      } else {
          // Scan 32-bit values
          _scan_and_encode_rom_indices_32(bcPtr, bcSize, romTablePtr, romTableSize);
      }

      // ... rest of compilation ...
});

// C implementation for 32-bit
void scan_and_encode_rom_indices_32(uint8_t* bc_buf, size_t bc_size,
void* rom_table, size_t rom_count) {
uint32_t* data = (uint32_t*)bc_buf;
size_t count = bc_size / sizeof(uint32_t);

      uintptr_t rom_start = (uintptr_t)rom_table;
      uintptr_t rom_end = rom_start + (rom_count * sizeof(JSAtomStruct));

      for (size_t i = 0; i < count; i++) {
          uint32_t value = data[i];
          if (value >= rom_start && value < rom_end) {
              size_t offset = value - rom_start;
              if (offset % sizeof(JSAtomStruct) == 0) {
                  uint16_t rom_idx = offset / sizeof(JSAtomStruct);
                  data[i] = encode_rom_index(rom_idx);
              }
          }
      }
}

// C implementation for 64-bit
void scan_and_encode_rom_indices_64(uint8_t* bc_buf, size_t bc_size,
void* rom_table, size_t rom_count) {
uint64_t* data = (uint64_t*)bc_buf;
size_t count = bc_size / sizeof(uint64_t);

      uintptr_t rom_start = (uintptr_t)rom_table;
      uintptr_t rom_end = rom_start + (rom_count * sizeof(JSAtomStruct));

      for (size_t i = 0; i < count; i++) {
          uint64_t value = data[i];
          if (value >= rom_start && value < rom_end) {
              size_t offset = value - rom_start;
              if (offset % sizeof(JSAtomStruct) == 0) {
                  uint16_t rom_idx = offset / sizeof(JSAtomStruct);
                  data[i] = encode_rom_index(rom_idx);
              }
          }
      }
}

Cross-Compilation Support

One interesting scenario: Compile on 64-bit WASM, run on 32-bit ESP32

// The bytecode carries its architecture in the version field
// 0x0001 = 32-bit, 0x8001 = 64-bit

// ESP32 rejects 64-bit bytecode:
JSValue JS_LoadBytecode(JSContext *ctx, const uint8_t *buf, size_t buf_len) {
const JSBytecodeHeader *hdr = (const JSBytecodeHeader *)buf;

      #if UINTPTR_MAX == 0xFFFFFFFF
          // 32-bit ESP32 only accepts 32-bit bytecode
          if (hdr->version != JS_BYTECODE_VERSION_32_V1) {
              return JS_ThrowSyntaxError(ctx,
                  "64-bit bytecode not supported on 32-bit architecture");
          }
      #else
          // 64-bit can accept both
          if (hdr->version != JS_BYTECODE_VERSION_32_V1 &&
              hdr->version != JS_BYTECODE_VERSION_64_V1) {
              return JS_ThrowSyntaxError(ctx, "invalid bytecode version");
          }
      #endif

      // ROM indices are architecture-independent!
      // Index 42 means the same thing on 32-bit and 64-bit

      // ... rest of load ...
}

Summary

Yes, it works for 64-bit! The key insight is:

1. ROM indices are universal - Index 42 is "setColor" on any architecture
2. Marker is architecture-specific - But the encoding/decoding logic is the same
3. Bytecode must match architecture - Can't run 64-bit bytecode on 32-bit ESP32
4. WASM compiler detects target - Generates appropriate marker based on version

The ROM index approach is actually more portable than the translation table because indices are truly universal across architectures!

> and if I want to integrate this in core mquickjs (WASM is *not* part of that, also not the emscripten version).

⏺ Ah, good point! For core mquickjs (just the C library, not WASM), the changes are actually very minimal. Core mquickjs only needs to decode ROM indices, not create them.

Core mquickjs Changes (Minimal Patch)

1. Add ROM Index Support to mquickjs.h

// mquickjs.h - Add these defines

// ROM index encoding (for cross-compilation support)
#if UINTPTR_MAX == 0xFFFFFFFF
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFF0000UL
#define JS_ATOM_ROM_INDEX_MASK   0x0000FFFFUL
#else
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFFFFFFFFFF0000ULL
#define JS_ATOM_ROM_INDEX_MASK   0x000000000000FFFFULL
#endif

// Check if a bytecode value is a ROM index (vs pointer/offset)
static inline int js_is_rom_index(uintptr_t value) {
return (value & ~JS_ATOM_ROM_INDEX_MASK) == JS_ATOM_ROM_INDEX_MARKER;
}

// Extract ROM index from encoded value
static inline uint16_t js_decode_rom_index(uintptr_t value) {
return (uint16_t)(value & JS_ATOM_ROM_INDEX_MASK);
}

2. Modify Relocation in mquickjs.c

// mquickjs.c - ONE function change

static uintptr_t bc_reloc_value(uintptr_t value, uintptr_t base_addr) {
// NEW: Check for ROM index encoding
if (js_is_rom_index(value)) {
uint16_t rom_idx = js_decode_rom_index(value);

          // Convert ROM index to actual ROM pointer
          extern JSAtomStruct *js_rom_atoms[];
          return (uintptr_t)&js_rom_atoms[rom_idx];
      }

      // EXISTING: Standard relocation
      if (value == 0)
          return 0;
      return base_addr + value;
}

// That's the only change! All other relocation code stays the same.

3. Optional: Document the Feature

/* mquickjs.h - Add comment to JS_RelocateBytecode2 */

/**
* Relocate bytecode to target address.
*
* Supports two relocation modes:
* 1. Standard: offsets → (base_addr + offset)
* 2. ROM indices: encoded indices → ROM pointers (for cross-compilation)
*
* ROM index encoding: 0xFFFF0000 | index (32-bit)
*                     0xFFFFFFFFFFFF0000 | index (64-bit)
*/
int JS_RelocateBytecode2(void *bc_buf, size_t bc_len,
uintptr_t base_addr, int update_atoms);

That's It for Core!

The core mquickjs changes are literally:
1. Add two inline helper functions (5 lines)
2. Add one if statement to bc_reloc_value() (6 lines)

Total: ~11 lines of code

Where Does Encoding Happen?

The encoding (ROM pointers → ROM indices) happens outside core mquickjs, in your tooling:

Option A: WASM Compiler (Your Current Setup)

// emscripten_wrapper.c (NOT part of core mquickjs)

// After mquickjs compiles to bytecode, scan and encode ROM atoms
void convert_rom_pointers_to_indices(uint8_t* bytecode, size_t size) {
uintptr_t rom_start = (uintptr_t)JS_GetStdlibAtomTable();
uintptr_t rom_end = rom_start + (JS_GetRomAtomTableSize() * sizeof(JSAtomStruct));

      uint32_t* data = (uint32_t*)bytecode;
      for (size_t i = 0; i < size / 4; i++) {
          if (data[i] >= rom_start && data[i] < rom_end) {
              uint16_t idx = (data[i] - rom_start) / sizeof(JSAtomStruct);
              data[i] = 0xFFFF0000 | idx;  // Encode as ROM index
          }
      }
}

Option B: Separate Tool (bytecode-portability-tool)

// standalone_converter.c (NOT part of core mquickjs)

// Command-line tool: convert native bytecode → portable bytecode
int main(int argc, char** argv) {
// Load bytecode file
uint8_t* bc = load_file(argv[1]);
size_t size = get_file_size(argv[1]);

      // Scan for ROM pointers and convert to indices
      convert_rom_pointers_to_indices(bc, size);

      // Save portable bytecode
      save_file(argv[2], bc, size);
}

Option C: Build-Time Hook in qjsc

// qjsc.c modification (optional, for convenience)

static int output_bytecode(const char* filename, uint8_t* bc, size_t size) {
if (portable_flag) {  // New --portable flag
// Convert ROM pointers to indices for cross-compilation
make_bytecode_portable(bc, size);
}

      // Write to file
      write_file(filename, bc, size);
}

Proposed Patch for Bellard

Here's what you'd submit as a minimal, non-invasive patch:

From: Your Name <your@email.com>
Subject: [PATCH] mquickjs: Add ROM index support for cross-compilation

This patch adds support for ROM index encoding in bytecode, enabling
cross-compilation scenarios where bytecode is compiled on one device
(e.g., browser WASM) and executed on another (e.g., ESP32).

Problem: ROM atom pointers differ between devices, causing compiled
bytecode to reference invalid memory addresses.

Solution: Allow bytecode to contain ROM indices (0xFFFF0000 | index)
instead of ROM pointers. The relocation code transparently converts
indices to device-specific ROM pointers.

This is backwards-compatible: existing bytecode with ROM pointers
continues to work unchanged.

  ---
mquickjs.h |  12 ++++++++++++
mquickjs.c |   8 ++++++++
2 files changed, 20 insertions(+)

diff --git a/mquickjs.h b/mquickjs.h
index 1234567..abcdefg 100644
--- a/mquickjs.h
+++ b/mquickjs.h
@@ -100,6 +100,18 @@ typedef struct JSBytecodeHeader {
JSValue main_func;
} JSBytecodeHeader;

+/* ROM index encoding for cross-compilation support */
+#if UINTPTR_MAX == 0xFFFFFFFF
+    #define JS_ATOM_ROM_INDEX_MARKER 0xFFFF0000UL
     +#else
+    #define JS_ATOM_ROM_INDEX_MARKER 0xFFFFFFFFFFFF0000ULL
     +#endif
     +#define JS_ATOM_ROM_INDEX_MASK 0x0000FFFFULL
+
+static inline int js_is_rom_index(uintptr_t value) {
+    return (value & ~JS_ATOM_ROM_INDEX_MASK) == JS_ATOM_ROM_INDEX_MARKER;
     +}
+
int JS_RelocateBytecode2(void *bc_buf, size_t bc_len,
uintptr_t base_addr, int update_atoms);

diff --git a/mquickjs.c b/mquickjs.c
index 7654321..fedcba9 100644
--- a/mquickjs.c
+++ b/mquickjs.c
@@ -450,6 +450,14 @@ static uintptr_t bc_reloc_value(uintptr_t value, uintptr_t base_addr)
{
+    /* Check for ROM index encoding (cross-compilation support) */
+    if (js_is_rom_index(value)) {
+        uint16_t rom_idx = (uint16_t)(value & JS_ATOM_ROM_INDEX_MASK);
+        extern JSAtomStruct *js_rom_atoms[];
+        return (uintptr_t)&js_rom_atoms[rom_idx];
+    }
+
+    /* Standard pointer relocation */
     if (value == 0)
     return 0;
     return base_addr + value;
     --
     2.40.0

Summary

Core mquickjs needs:
- Minimal changes (~20 lines)
- Just decode support, no encoding
- Completely backwards compatible

Your WASM/tooling handles:
- Scanning bytecode for ROM pointers
- Converting ROM pointers → ROM indices
- All the complex logic

This separation of concerns makes the patch:
1. Simple - Easy to review and accept
2. Non-invasive - Doesn't change existing behavior
3. Flexible - Encoding strategy is up to the tooling
4. Universal - Works for WASM, native, or any cross-compilation scenario

Does this approach make sense for your use case?

> it needs to store the indices in bytecode right? *ignore* wasm now

⏺ Yes, exactly! The ROM indices are stored directly in the bytecode instruction stream, replacing the ROM pointers.

Where ROM Atoms Appear in Bytecode

QuickJS bytecode is a stream of instructions and operands:

[Header - 20 bytes]

[Bytecode instruction stream:]
OP_get_var
<atom>          ← ROM atom here (e.g., "console")
OP_get_field
<atom>          ← ROM atom here (e.g., "log")
OP_push_const
<string_ref>
OP_call
<arg_count>
...

Example: console.log("hello")

Before (with ROM pointers):
Offset  Instruction          Value
  ------  -------------------  ------------------
0x0100  OP_get_var
0x0101  atom                 0x3c0a4320  ← ROM pointer to "console"
0x0105  OP_get_field
0x0106  atom                 0x3c0a4458  ← ROM pointer to "log"
0x010a  OP_push_const
0x010b  string_ref           0x00000234  ← Offset to "hello"
0x010f  OP_call
0x0110  arg_count            1

After (with ROM indices):
Offset  Instruction          Value
  ------  -------------------  ------------------
0x0100  OP_get_var
0x0101  atom                 0xFFFF002A  ← ROM index 42 ("console")
0x0105  OP_get_field
0x0106  atom                 0xFFFF0057  ← ROM index 87 ("log")
0x010a  OP_push_const
0x010b  string_ref           0x00000234  ← Regular offset (unchanged)
0x010f  OP_call
0x0110  arg_count            1

The Conversion Process

Step 1: Compile to Bytecode (Native or Cross-Platform)

// On ANY device (PC, ESP32, or WASM)
JSValue obj = JS_Eval(ctx, "console.log('hello')", ...);
uint8_t* bytecode = JS_WriteObject(ctx, &size, obj, JS_WRITE_OBJ_BYTECODE);

// At this point, bytecode contains ROM pointers from THIS device

Step 2: Convert ROM Pointers → ROM Indices

// Scan the bytecode and replace ROM pointers with indices
void make_bytecode_portable(uint8_t* bc, size_t size,
JSAtomStruct* rom_table, size_t rom_count) {
uintptr_t rom_start = (uintptr_t)rom_table;
uintptr_t rom_end = rom_start + (rom_count * sizeof(JSAtomStruct));

      // Scan every 4-byte word in bytecode (or 8-byte for 64-bit)
      uint32_t* data = (uint32_t*)(bc + sizeof(JSBytecodeHeader));
      size_t word_count = (size - sizeof(JSBytecodeHeader)) / sizeof(uint32_t);

      for (size_t i = 0; i < word_count; i++) {
          uint32_t value = data[i];

          // Is this value a ROM pointer?
          if (value >= rom_start && value < rom_end) {
              // Calculate which ROM atom this points to
              size_t offset = value - rom_start;
              if (offset % sizeof(JSAtomStruct) == 0) {
                  uint16_t rom_idx = offset / sizeof(JSAtomStruct);

                  // Replace pointer with encoded index
                  data[i] = 0xFFFF0000 | rom_idx;
              }
          }
      }
}

Step 3: Load on Target Device

// On ESP32 (or any target device)
const uint8_t* bc = mmap_from_flash(...);  // Contains ROM indices

// When relocation processes the bytecode:
static uintptr_t bc_reloc_value(uintptr_t value, uintptr_t base_addr) {
// Detect ROM index
if ((value & 0xFFFF0000) == 0xFFFF0000) {
uint16_t rom_idx = value & 0xFFFF;

          // Convert to THIS device's ROM pointer
          extern JSAtomStruct* js_rom_atoms[];
          return (uintptr_t)&js_rom_atoms[rom_idx];  // ESP32 ROM pointer
      }

      // Regular offset relocation
      return base_addr + value;
}

Key Point: In-Place Replacement

The ROM indices are stored in the exact same locations where ROM pointers would normally be. This is why the approach works:

1. No format change - Bytecode structure stays identical
2. Transparent - Relocation code doesn't need to know bytecode instruction formats
3. Efficient - No extra tables, no extra memory
4. Universal - Works for any instruction that references ROM atoms

Complete Flow (No WASM)

┌─────────────────┐
│  Device A (PC)  │
│  Compile JS     │
│  ↓              │
│  Bytecode with  │
│  PC ROM ptrs    │  0x7f8a2340 (console)
└────────┬────────┘
│
│ make_bytecode_portable()
↓
┌────────────┐
│  Portable  │
│  Bytecode  │  0xFFFF002A (index 42)
└─────┬──────┘
│ Transfer to Device B
↓
┌──────────────────┐
│ Device B (ESP32) │
│ Load bytecode    │
│ ↓                │
│ bc_reloc_value() │
│ ↓                │
│ 0xFFFF002A       │
│ → index 42       │
│ → 0x3c0a4320     │  ESP32 ROM pointer
└──────────────────┘

So yes, the indices are embedded directly in the bytecode data, exactly where ROM pointers would have been. No separate table, no header changes - just in-place replacement of pointers with encoded indices.

> explain to me how mquickjs (not *wasm*) generates bytecode with ROM indices as of the Atoms

⏺ Good question! Let me clarify: mquickjs core doesn't currently generate ROM indices natively. The ROM index approach requires a post-processing step after bytecode generation.

How mquickjs Currently Generates Bytecode

Step 1: Compilation (Runtime Representation)

// When you compile JavaScript
JSValue func = JS_Eval(ctx, "console.log('hello')", ...);

// Internally, the bytecode uses ROM POINTERS for fast execution:
// [OP_get_var] [atom: 0x3c0a4320]  ← Direct pointer to ROM atom "console"

During compilation and execution, mquickjs uses actual ROM pointers because they're fast - no lookup needed, just direct memory access.

Step 2: Serialization (File Format)

// When you serialize to a file
size_t size;
uint8_t* bytecode = JS_WriteObject(ctx, &size, func, JS_WRITE_OBJ_BYTECODE);

// The serialized bytecode STILL contains ROM pointers:
// [Header] [OP_get_var] [atom: 0x3c0a4320] ...

This is the problem: The serialized bytecode has device-specific ROM pointers baked in.

Two Approaches to Generate ROM Indices

Approach 1: Post-Processing (Current Implementation)

Convert ROM pointers → ROM indices after serialization:

// Step 1: Generate normal bytecode
uint8_t* bytecode = JS_WriteObject(ctx, &size, func, JS_WRITE_OBJ_BYTECODE);

// Step 2: Post-process to replace ROM pointers with indices
void make_bytecode_portable(uint8_t* bc, size_t size) {
// Get ROM table bounds
extern JSAtomStruct* js_rom_atoms[];
extern size_t js_rom_atom_count;

      uintptr_t rom_start = (uintptr_t)js_rom_atoms;
      uintptr_t rom_end = rom_start + (js_rom_atom_count * sizeof(JSAtomStruct));

      // Scan bytecode and replace ROM pointers
      uint32_t* data = (uint32_t*)(bc + sizeof(JSBytecodeHeader));
      size_t words = (size - sizeof(JSBytecodeHeader)) / 4;

      for (size_t i = 0; i < words; i++) {
          uint32_t value = data[i];

          // Is this a ROM pointer?
          if (value >= rom_start && value < rom_end) {
              // Calculate index
              uint16_t idx = (value - rom_start) / sizeof(JSAtomStruct);

              // Replace with encoded index
              data[i] = 0xFFFF0000 | idx;
          }
      }
}

// Usage:
uint8_t* bc = JS_WriteObject(ctx, &size, func, JS_WRITE_OBJ_BYTECODE);
make_bytecode_portable(bc, size);  // Now contains ROM indices
save_to_file("script.jsc", bc, size);

Pros:
- Simple, non-invasive
- Works with any mquickjs build
- No changes to core serialization

Cons:
- Extra processing step
- Scans entire bytecode

Approach 2: Native Generation (Modify Serialization)

Modify JS_WriteObject to emit ROM indices during serialization:

// In quickjs.c - modify the BC writer

// Current code (simplified):
static int BC_WriteAtom(BCWriterState *s, JSAtom atom) {
// Write atom value directly (which is a ROM pointer)
return bc_put_u32(s, (uint32_t)atom);
}

// Modified code:
static int BC_WriteAtom(BCWriterState *s, JSAtom atom) {
// Check if this atom is a ROM atom
extern JSAtomStruct* js_rom_atoms[];
extern size_t js_rom_atom_count;

      uintptr_t atom_ptr = (uintptr_t)JS_VALUE_GET_PTR(atom);
      uintptr_t rom_start = (uintptr_t)js_rom_atoms;
      uintptr_t rom_end = rom_start + (js_rom_atom_count * sizeof(JSAtomStruct));

      if (atom_ptr >= rom_start && atom_ptr < rom_end) {
          // This is a ROM atom - write as index
          uint16_t rom_idx = (atom_ptr - rom_start) / sizeof(JSAtomStruct);
          uint32_t encoded = 0xFFFF0000 | rom_idx;
          return bc_put_u32(s, encoded);
      }

      // Regular atom - write as-is
      return bc_put_u32(s, (uint32_t)atom);
}

Pros:
- Clean, happens during serialization
- No post-processing needed
- More efficient

Cons:
- Requires modifying mquickjs core
- More invasive change
- Need to identify all places atoms are written

Why Post-Processing is Simpler

The challenge with Approach 2 is that atoms appear in many places in bytecode:

// In quickjs.c, atoms are written in multiple functions:
BC_WriteAtom()        // Direct atom references
BC_WriteFunctionTag() // Function names
BC_WriteString()      // String literals (may reference ROM strings)
BC_WriteObject()      // Object property names
// ... many more

You'd need to modify ALL of these to check for ROM atoms.

Post-processing is simpler because:
- We don't need to understand bytecode structure
- Just scan for values in ROM address range
- Works regardless of where ROM pointers appear

Complete Example (Post-Processing Approach)

// compile.c - Tool to compile JS to portable bytecode

#include "mquickjs.h"

int main(int argc, char** argv) {
JSRuntime* rt = JS_NewRuntime();
JSContext* ctx = JS_NewContext(rt);

      // 1. Compile JavaScript
      const char* source = load_file(argv[1]);
      JSValue func = JS_Eval(ctx, source, strlen(source),
                             "<input>", JS_EVAL_TYPE_GLOBAL);

      // 2. Serialize to bytecode (contains ROM pointers)
      size_t size;
      uint8_t* bytecode = JS_WriteObject(ctx, &size, func,
                                         JS_WRITE_OBJ_BYTECODE);

      // 3. Convert ROM pointers to indices
      make_bytecode_portable(bytecode, size);

      // 4. Save portable bytecode
      save_file(argv[2], bytecode, size);

      printf("Generated portable bytecode: %zu bytes\n", size);

      JS_FreeValue(ctx, func);
      JS_FreeContext(ctx);
      JS_FreeRuntime(rt);
      return 0;
}

Summary

mquickjs core generates bytecode with ROM pointers - this is by design for runtime performance.

To get ROM indices, you have two options:

1. Post-process (recommended): Scan and convert after JS_WriteObject()
2. Modify serialization: Change BC_WriteAtom() and related functions

For a minimal patch to upstream mquickjs, I'd recommend:
- Keep post-processing separate (in tooling/wrapper layer)
- Only patch core with decoding support (the bc_reloc_value change)
- Let users decide how to generate portable bytecode (post-processing tool, WASM wrapper, etc.)

This keeps the core patch minimal and flexible.

> what would it look like for serialization

⏺ Here's what it would look like to modify the serialization process to emit ROM indices natively:

Core Serialization Modification

1. Add ROM Detection Helper (mquickjs.c)

// mquickjs.c - Add near the BC writer functions

typedef struct {
JSAtomStruct* table;
size_t count;
uintptr_t start;
uintptr_t end;
} ROMAtomInfo;

// Initialize ROM atom info for serialization
static void init_rom_atom_info(ROMAtomInfo* info) {
extern JSAtomStruct* js_rom_atoms[];
extern size_t js_rom_atom_count;

      info->table = js_rom_atoms;
      info->count = js_rom_atom_count;
      info->start = (uintptr_t)js_rom_atoms;
      info->end = info->start + (js_rom_atom_count * sizeof(JSAtomStruct));
}

// Check if a pointer is a ROM atom and get its index
static int get_rom_atom_index(ROMAtomInfo* info, uintptr_t ptr, uint16_t* out_idx) {
if (ptr >= info->start && ptr < info->end) {
size_t offset = ptr - info->start;
if (offset % sizeof(JSAtomStruct) == 0) {
*out_idx = offset / sizeof(JSAtomStruct);
return 1;  // Is ROM atom
}
}
return 0;  // Not ROM atom
}

2. Modify BCWriterState Structure

// mquickjs.c - Modify the bytecode writer state

typedef struct BCWriterState {
JSContext *ctx;
DynBuf dbuf;
BOOL byte_swap : 8;
BOOL allow_bytecode : 8;
BOOL allow_sab : 8;
BOOL allow_reference : 8;

      // ADD THIS:
      ROMAtomInfo rom_info;      // ROM atom information
      BOOL portable_mode : 8;    // Emit ROM indices instead of pointers

      // ... rest of structure
} BCWriterState;

3. Modify bc_put_u32 for ROM Atoms

// mquickjs.c - Add specialized function for writing atoms

static int bc_put_atom(BCWriterState *s, uintptr_t atom_ptr) {
uint16_t rom_idx;

      // If portable mode and this is a ROM atom, write index
      if (s->portable_mode && get_rom_atom_index(&s->rom_info, atom_ptr, &rom_idx)) {
          // Encode ROM index with marker
          uint32_t encoded = JS_ATOM_ROM_INDEX_MARKER | rom_idx;
          return bc_put_u32(s, encoded);
      }

      // Otherwise write the pointer/value as-is
      return bc_put_u32(s, (uint32_t)atom_ptr);
}

4. Find and Replace All Atom Writes

This is the tedious part - you need to find everywhere atoms are written:

// mquickjs.c - Example locations that need modification

// Location 1: Writing atom directly in bytecode
static int BC_WriteFunctionTag(BCWriterState *s, JSValue obj) {
// ... existing code ...

      // OLD:
      // bc_put_u32(s, (uint32_t)atom_ptr);

      // NEW:
      bc_put_atom(s, atom_ptr);

      // ... rest of function ...
}

// Location 2: Writing atoms in constant pool
static int BC_WriteAtomValue(BCWriterState *s, JSAtom atom) {
JSAtomStruct *p = JS_VALUE_GET_PTR(JS_MKPTR(JS_TAG_STRING, atom));

      // OLD:
      // bc_put_u32(s, (uint32_t)p);

      // NEW:
      bc_put_atom(s, (uintptr_t)p);

      return 0;
}

// Location 3: Writing property names
static int BC_WriteObjectProperties(BCWriterState *s, JSObject *obj) {
for (int i = 0; i < obj->prop_count; i++) {
JSAtom atom = obj->properties[i].atom;

          // OLD:
          // bc_put_u32(s, (uint32_t)atom);

          // NEW:
          bc_put_atom(s, (uintptr_t)atom);
      }
      return 0;
}

// There are many more locations - need to audit the entire BC writer

5. Modify JS_WriteObject to Support Portable Mode

// mquickjs.c - Add flag to JS_WriteObject

// Add new flag
#define JS_WRITE_OBJ_PORTABLE  (1 << 4)  // Emit ROM indices for portability

uint8_t *JS_WriteObject(JSContext *ctx, size_t *psize, JSValueConst obj,
int flags)
{
BCWriterState ss, *s = &ss;

      // ... existing initialization ...

      // NEW: Initialize ROM atom tracking and portable mode
      if (flags & JS_WRITE_OBJ_PORTABLE) {
          s->portable_mode = TRUE;
          init_rom_atom_info(&s->rom_info);
      } else {
          s->portable_mode = FALSE;
      }

      // ... rest of function ...
}

6. Usage Example

// Application code - compile to portable bytecode

int compile_to_portable_bytecode(const char* js_source, const char* output_file) {
JSRuntime* rt = JS_NewRuntime();
JSContext* ctx = JS_NewContext(rt);

      // Compile JavaScript
      JSValue func = JS_Eval(ctx, js_source, strlen(js_source),
                             "<input>", JS_EVAL_TYPE_GLOBAL);

      if (JS_IsException(func)) {
          js_std_dump_error(ctx);
          return -1;
      }

      // Serialize with PORTABLE flag
      size_t size;
      uint8_t* bytecode = JS_WriteObject(ctx, &size, func,
                                         JS_WRITE_OBJ_BYTECODE |
                                         JS_WRITE_OBJ_PORTABLE);  // ← NEW FLAG

      // Bytecode now contains ROM indices!
      save_file(output_file, bytecode, size);

      js_free(ctx, bytecode);
      JS_FreeValue(ctx, func);
      JS_FreeContext(ctx);
      JS_FreeRuntime(rt);

      return 0;
}

Complete Patch Structure

Here's what the full patch would look like:

// ============================================================================
// PART 1: mquickjs.h - Public API
// ============================================================================

// Add new write flag
#define JS_WRITE_OBJ_PORTABLE  (1 << 4)

// Add ROM index encoding (same as before)
#if UINTPTR_MAX == 0xFFFFFFFF
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFF0000UL
#else
#define JS_ATOM_ROM_INDEX_MARKER 0xFFFFFFFFFFFF0000ULL
#endif
#define JS_ATOM_ROM_INDEX_MASK 0x0000FFFFULL

static inline int js_is_rom_index(uintptr_t value) {
return (value & ~JS_ATOM_ROM_INDEX_MASK) == JS_ATOM_ROM_INDEX_MARKER;
}


// ============================================================================
// PART 2: mquickjs.c - Serialization Changes
// ============================================================================

// 1. ROM atom tracking
typedef struct {
uintptr_t start;
uintptr_t end;
} ROMAtomInfo;

static void init_rom_atom_info(ROMAtomInfo* info) {
extern JSAtomStruct* js_rom_atoms[];
extern size_t js_rom_atom_count;
info->start = (uintptr_t)js_rom_atoms;
info->end = info->start + (js_rom_atom_count * sizeof(JSAtomStruct));
}

static int get_rom_atom_index(ROMAtomInfo* info, uintptr_t ptr,
uint16_t* out_idx) {
if (ptr >= info->start && ptr < info->end) {
size_t offset = ptr - info->start;
if (offset % sizeof(JSAtomStruct) == 0) {
*out_idx = offset / sizeof(JSAtomStruct);
return 1;
}
}
return 0;
}

// 2. Modify BCWriterState
typedef struct BCWriterState {
// ... existing fields ...
ROMAtomInfo rom_info;
BOOL portable_mode : 8;
} BCWriterState;

// 3. Add atom writer
static int bc_put_atom(BCWriterState *s, uintptr_t atom_ptr) {
uint16_t rom_idx;
if (s->portable_mode && get_rom_atom_index(&s->rom_info, atom_ptr, &rom_idx)) {
uint32_t encoded = JS_ATOM_ROM_INDEX_MARKER | rom_idx;
return bc_put_u32(s, encoded);
}
return bc_put_u32(s, (uint32_t)atom_ptr);
}

// 4. Modify JS_WriteObject
uint8_t *JS_WriteObject(JSContext *ctx, size_t *psize, JSValueConst obj,
int flags) {
BCWriterState ss, *s = &ss;

      // ... existing init ...

      // Initialize portable mode
      s->portable_mode = (flags & JS_WRITE_OBJ_PORTABLE) != 0;
      if (s->portable_mode) {
          init_rom_atom_info(&s->rom_info);
      }

      // ... rest of function ...
}

// 5. Replace all bc_put_u32 calls for atoms with bc_put_atom
// (This is the tedious part - need to audit entire BC writer)


// ============================================================================
// PART 3: mquickjs.c - Deserialization (same as before)
// ============================================================================

static uintptr_t bc_reloc_value(uintptr_t value, uintptr_t base_addr) {
if (js_is_rom_index(value)) {
uint16_t rom_idx = (uint16_t)(value & JS_ATOM_ROM_INDEX_MASK);
extern JSAtomStruct *js_rom_atoms[];
return (uintptr_t)&js_rom_atoms[rom_idx];
}
if (value == 0)
return 0;
return base_addr + value;
}

Challenges with This Approach

1. Finding All Atom Write Locations

You need to audit the entire BC writer and find every place where atoms are written:

# Search for all bc_put_u32 calls that might be atoms
grep -n "bc_put_u32.*atom" mquickjs.c
grep -n "bc_put_u32.*JS_ATOM" mquickjs.c
grep -n "bc_put_u32.*(uint32_t).*p" mquickjs.c  # Atom struct pointers

2. Distinguishing Atoms from Other uint32_t Values

Not every bc_put_u32() call is an atom:

bc_put_u32(s, instruction_count);   // Not an atom
bc_put_u32(s, constant_value);      // Not an atom
bc_put_u32(s, (uint32_t)atom_ptr);  // THIS is an atom!

You need to carefully identify which values are atoms.

3. 64-bit Support

Need to handle both 32-bit and 64-bit:

#if UINTPTR_MAX == 0xFFFFFFFF
static int bc_put_atom(BCWriterState *s, uintptr_t atom_ptr) {
uint16_t rom_idx;
if (s->portable_mode && get_rom_atom_index(&s->rom_info, atom_ptr, &rom_idx)) {
return bc_put_u32(s, 0xFFFF0000 | rom_idx);
}
return bc_put_u32(s, (uint32_t)atom_ptr);
}
#else
static int bc_put_atom(BCWriterState *s, uintptr_t atom_ptr) {
uint16_t rom_idx;
if (s->portable_mode && get_rom_atom_index(&s->rom_info, atom_ptr, &rom_idx)) {
return bc_put_u64(s, 0xFFFFFFFFFFFF0000ULL | rom_idx);
}
return bc_put_u64(s, atom_ptr);
}
#endif

Recommendation

For upstream contribution, I'd recommend the post-processing approach because:

1. Less invasive - Only ~20 lines changed in core
2. Safer - Don't need to audit entire BC writer
3. Flexible - Users can choose when to make bytecode portable
4. Testable - Easy to verify correctness

The serialization approach is cleaner architecturally but much more complex to implement correctly.

