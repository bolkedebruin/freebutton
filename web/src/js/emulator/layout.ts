/**
 * Layout of the C structs the host writes into wasm memory, as wasm32 lays
 * them out (4-byte int, long and pointer, like the ESP32).
 *
 *   DisplayItemInfo   src/scripting/displayitem_binding.h (displayitem_get_info)
 *   SensorInfo        src/scripting/sensor_binding.h (filled by the firmware's
 *                     sensor_binding.cpp inside the wasm; listed for the test)
 *
 * The wasm exports fbe_layout(i): sizeof / offsetof of the same fields, from
 * the headers it was built with. emulator-layout.test.ts compares them with
 * this table in LAYOUT_ORDER, so a header change breaks the test.
 */
export const DISPLAY_ITEM_INFO = {
  size: 44,
  id: 0,
  x: 4,
  y: 8,
  width: 12,
  boxType: 16,
  fontSize: 20,
  alignment: 24,
  /** const char*: NUL-terminated text. */
  label: 28,
  /** const char* */
  unit: 32,
  round: 36,
  page: 40,
} as const;

export const SENSOR_INFO = {
  size: 20,
  id: 0,
  name: 4,
  type: 8,
  unit: 12,
  online: 16,
} as const;

/** sizeof(unsigned long), sizeof(void*). */
export const C_TYPES = { long: 4, pointer: 4 } as const;

/** What fbe_layout(0), fbe_layout(1), ... return, as this table's values. */
export const LAYOUT_ORDER: readonly number[] = [
  DISPLAY_ITEM_INFO.size,
  DISPLAY_ITEM_INFO.id,
  DISPLAY_ITEM_INFO.x,
  DISPLAY_ITEM_INFO.y,
  DISPLAY_ITEM_INFO.width,
  DISPLAY_ITEM_INFO.boxType,
  DISPLAY_ITEM_INFO.fontSize,
  DISPLAY_ITEM_INFO.alignment,
  DISPLAY_ITEM_INFO.label,
  DISPLAY_ITEM_INFO.unit,
  DISPLAY_ITEM_INFO.round,
  DISPLAY_ITEM_INFO.page,
  C_TYPES.long,
  C_TYPES.pointer,
  SENSOR_INFO.size,
  SENSOR_INFO.id,
  SENSOR_INFO.name,
  SENSOR_INFO.type,
  SENSOR_INFO.unit,
  SENSOR_INFO.online,
];

/** fbe_limit(i): the firmware's limits compiled into the wasm. */
export const LIMIT = {
  brokersWithSubscriptions: 0,
  subscriptionsPerBroker: 1,
  timers: 2,
  keys: 3,
  sensors: 4,
  displayItemsListed: 5,
  mqttTopicLen: 6,
  mqttPayloadLen: 7,
  mqttQueueBytes: 8,
  mqttInBufferBytes: 9,
  keyScanMs: 10,
  loopDelayMs: 11,
  sourceBytes: 12,
  bytecodeBytes: 13,
  leds: 14,
} as const;
