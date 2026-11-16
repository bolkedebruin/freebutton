/**
 * fbe_limit(i): the firmware's limits compiled into the wasm.
 *
 * (Stage 1 also had the layout of C structs here that the host wrote into
 * wasm memory. Since stage 2 the bindings behind those structs are the
 * firmware's own, inside the wasm; the host exchanges strings and JSON.)
 */
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
