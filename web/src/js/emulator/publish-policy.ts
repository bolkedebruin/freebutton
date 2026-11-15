/**
 * Publish safety for the emulator (pure; no transport, no engine).
 *
 * The emulator runs a real device's script. Connected to a real broker, a
 * careless publish would switch real things (a key press reaching Home
 * Assistant) or put retained state on the broker in the name of a device that
 * does not exist. Every publish, from the script or from the firmware's own
 * side (Home Assistant discovery, availability), goes through decidePublish().
 *
 * Modes:
 *   offline         nothing leaves the page; the in-page MockBroker gets the
 *                   publish as it is
 *   live-readonly   connected to a real broker, subscriptions only; every
 *                   publish is blocked and logged as "would publish"
 *   live-publish    publishes go out, under these rules:
 *                   1. identity: the real device's id in a topic becomes the
 *                      virtual identity (virt_btn_xxxxxx)
 *                   2. Home Assistant discovery and availability topics (what
 *                      the firmware publishes by itself) are blocked unless
 *                      allowDiscovery
 *                   3. a topic that is not under the virtual identity is a
 *                      topic written literally in config.json or the script:
 *                      blocked unless it matches an entry of allowTopics
 *                   4. retain is cleared unless allowRetain
 *
 * Stage 1 uses only `offline` (MockBroker); the live modes are here so the
 * WebSocket transport of stage 3 has nothing to decide.
 */

export type PublishMode = 'offline' | 'live-readonly' | 'live-publish';

/** The emulator's identity: never a real device's ("btn_" + 6 hex of the MAC). */
export const VIRTUAL_ID_PATTERN = /^virt_btn_[0-9a-f]{6}$/;
/** A real device id (src/main.cpp deviceId). */
export const DEVICE_ID_PATTERN = /^btn_[0-9a-f]{6}$/;

/** Home Assistant's discovery prefix (src/mqtt/HomeAssistantDiscovery). */
export const HA_DISCOVERY_PREFIX = 'homeassistant/';
/** config.h BASE_TOPIC: <BASE_TOPIC><device id>/status is the availability (birth / last will) topic. */
export const BASE_TOPIC = 'buttonplus/';

/** A new virtual identity: "virt_btn_" + 6 hex digits. random: 0 <= n < 1. */
export function virtualIdentity(random: () => number = Math.random): string {
  let hex = '';
  for (let i = 0; i < 6; i++) hex += Math.floor(random() * 16).toString(16);
  return 'virt_btn_' + hex;
}

export function isVirtualIdentity(id: string): boolean {
  return VIRTUAL_ID_PATTERN.test(id);
}

export interface PublishRequest {
  broker: number;
  topic: string;
  payload: string;
  qos: number;
  retain: boolean;
  /** 'script': mqtt.publish(); 'firmware': what the firmware sends by itself (discovery, availability, key events). */
  origin: 'script' | 'firmware';
}

export interface PublishPolicy {
  mode: PublishMode;
  /** The emulator's identity (virt_btn_xxxxxx). */
  virtualId: string;
  /** The device the config and script were written for (btn_xxxxxx); its id in a topic is rewritten. */
  realId?: string;
  /** MQTT topic filters ('+', '#') for literal topics that may be published in live-publish. */
  allowTopics?: readonly string[];
  /** live-publish: let Home Assistant discovery and availability through. */
  allowDiscovery?: boolean;
  /** live-publish: keep the retain flag. */
  allowRetain?: boolean;
}

export type PublishAction = 'mock' | 'send' | 'block';

export interface PublishDecision {
  /** mock: to the in-page broker; send: to the real broker; block: nowhere. */
  action: PublishAction;
  /** The topic to use (identity rewritten in live-publish). */
  topic: string;
  retain: boolean;
  /** The topic was changed by the identity rule. */
  rewritten: boolean;
  /** The retain flag was cleared. */
  retainCleared: boolean;
  /** Why, in a word: 'offline' | 'read-only' | 'discovery' | 'not-allowed' | 'own-topic' | 'allowed'. */
  reason: 'offline' | 'read-only' | 'discovery' | 'not-allowed' | 'own-topic' | 'allowed';
  /** For a blocked publish: the console line ("would publish ..."). */
  log?: string;
}

/** MQTT filter match; the emulator passes the firmware's (fbe_topic_matches). */
export type TopicMatcher = (filter: string, topic: string) => boolean;

function escapeRegExp(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/**
 * Replaces the real device's id by the virtual identity wherever it stands
 * as a whole token in the topic ("buttonplus/btn_a00001/key" and
 * "homeassistant/event/btn_a00001_key1/config", not "xbtn_a000011").
 */
export function rewriteIdentity(topic: string, policy: Pick<PublishPolicy, 'virtualId' | 'realId'>): string {
  const real = policy.realId;
  if (!real || real === policy.virtualId) return topic;
  return topic.replace(new RegExp(`(^|[^A-Za-z0-9])${escapeRegExp(real)}(?![0-9A-Za-z])`, 'g'), `$1${policy.virtualId}`);
}

/** The topic names the virtual identity as a whole token: it is the emulator's own. */
export function isOwnTopic(topic: string, virtualId: string): boolean {
  return new RegExp(`(^|[^A-Za-z0-9])${escapeRegExp(virtualId)}(?![0-9A-Za-z])`).test(topic);
}

/**
 * Topics the firmware publishes by itself: Home Assistant discovery
 * (homeassistant/...) and the availability topic of a device
 * (buttonplus/<id>/status).
 */
export function isDiscoveryOrAvailability(topic: string): boolean {
  if (topic.startsWith(HA_DISCOVERY_PREFIX)) return true;
  if (!topic.startsWith(BASE_TOPIC)) return false;
  const rest = topic.slice(BASE_TOPIC.length);
  const slash = rest.indexOf('/');
  return slash > 0 && rest.slice(slash) === '/status';
}

const exactMatch: TopicMatcher = (filter, topic) => filter === topic;

function wouldPublish(request: PublishRequest, topic: string, why: string): string {
  const flags = `qos ${request.qos}${request.retain ? ', retain' : ''}`;
  return `would publish to broker ${request.broker}: ${topic} = ${JSON.stringify(request.payload)} (${flags}); blocked: ${why}`;
}

/** What happens to one publish. Pure. matches: filter match for allowTopics (default: exact topics only). */
export function decidePublish(
  request: PublishRequest, policy: PublishPolicy, matches: TopicMatcher = exactMatch,
): PublishDecision {
  const plain = { topic: request.topic, retain: request.retain, rewritten: false, retainCleared: false };
  if (policy.mode === 'offline') {
    return { action: 'mock', ...plain, reason: 'offline' };
  }
  if (policy.mode === 'live-readonly') {
    return {
      action: 'block', ...plain, reason: 'read-only',
      log: wouldPublish(request, request.topic, 'the emulator is connected read-only'),
    };
  }

  const topic = rewriteIdentity(request.topic, policy);
  const rewritten = topic !== request.topic;
  const base = { topic, rewritten };

  if (isDiscoveryOrAvailability(topic) && !policy.allowDiscovery) {
    return {
      action: 'block', ...base, retain: request.retain, retainCleared: false, reason: 'discovery',
      log: wouldPublish(request, topic, 'Home Assistant discovery and availability are off for the emulator'),
    };
  }

  const own = isOwnTopic(topic, policy.virtualId);
  if (!own && !(policy.allowTopics ?? []).some(filter => matches(filter, topic))) {
    return {
      action: 'block', ...base, retain: request.retain, retainCleared: false, reason: 'not-allowed',
      log: wouldPublish(request, topic, 'a topic outside the virtual device; add it to the allow list to publish it'),
    };
  }

  const retain = request.retain && policy.allowRetain === true;
  return {
    action: 'send', ...base, retain, retainCleared: request.retain && !retain,
    reason: own ? 'own-topic' : 'allowed',
  };
}
