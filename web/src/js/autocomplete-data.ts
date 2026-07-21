/**
 * Autocomplete Data for FreeButton Editor
 * Auto-generated from stdlib.json
 * DO NOT EDIT MANUALLY - Run: npm run generate-autocomplete
 */

export interface CompletionItem {
    label: string;
    type: string;
    detail: string;
    info: string;
    apply: string;
}

export interface APIDefinition {
    methods: CompletionItem[];
}

export interface FreebuttonAPIDefinition {
    [key: string]: APIDefinition;
}

export const freebuttonAPI: FreebuttonAPIDefinition = {
  "console": {
    "methods": [
      {
        "label": "console.log",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "console.log()"
      }
    ]
  },
  "mqtt": {
    "methods": [
      {
        "label": "mqtt.getBrokerCount",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.getBrokerCount()"
      },
      {
        "label": "mqtt.getBrokerName",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.getBrokerName()"
      },
      {
        "label": "mqtt.isConnected",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.isConnected()"
      },
      {
        "label": "mqtt.publish",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.publish()"
      },
      {
        "label": "mqtt.subscribe",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.subscribe()"
      },
      {
        "label": "mqtt.unsubscribe",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.unsubscribe()"
      },
      {
        "label": "mqtt.onConnect",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.onConnect()"
      },
      {
        "label": "mqtt.onDisconnect",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "mqtt.onDisconnect()"
      }
    ]
  },
  "performance": {
    "methods": [
      {
        "label": "performance.now",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "performance.now()"
      }
    ]
  },
  "sensor": {
    "methods": [
      {
        "label": "sensor.count",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "sensor.count()"
      },
      {
        "label": "sensor.getValue",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "sensor.getValue()"
      },
      {
        "label": "sensor.getType",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "sensor.getType()"
      },
      {
        "label": "sensor.getInfo",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "sensor.getInfo()"
      },
      {
        "label": "sensor.getAll",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "sensor.getAll()"
      },
      {
        "label": "sensor.onChange",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "sensor.onChange()"
      }
    ]
  },
  "system": {
    "methods": [
      {
        "label": "system.uptime",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "system.uptime()"
      },
      {
        "label": "system.freeHeap",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "system.freeHeap()"
      },
      {
        "label": "system.version",
        "type": "method",
        "detail": "() → void",
        "info": "",
        "apply": "system.version()"
      }
    ]
  },
  "Array": {
    "methods": [
      {
        "label": "Array.isArray",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Array.isArray()"
      }
    ]
  },
  "Date": {
    "methods": [
      {
        "label": "Date.now",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Date.now()"
      }
    ]
  },
  "JSON": {
    "methods": [
      {
        "label": "JSON.parse",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "JSON.parse()"
      },
      {
        "label": "JSON.stringify",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "JSON.stringify()"
      }
    ]
  },
  "Math": {
    "methods": [
      {
        "label": "Math.min",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.min()"
      },
      {
        "label": "Math.max",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.max()"
      },
      {
        "label": "Math.sign",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.sign()"
      },
      {
        "label": "Math.abs",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.abs()"
      },
      {
        "label": "Math.floor",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.floor()"
      },
      {
        "label": "Math.ceil",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.ceil()"
      },
      {
        "label": "Math.round",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.round()"
      },
      {
        "label": "Math.sqrt",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.sqrt()"
      },
      {
        "label": "Math.sin",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.sin()"
      },
      {
        "label": "Math.cos",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.cos()"
      },
      {
        "label": "Math.tan",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.tan()"
      },
      {
        "label": "Math.asin",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.asin()"
      },
      {
        "label": "Math.acos",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.acos()"
      },
      {
        "label": "Math.atan",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.atan()"
      },
      {
        "label": "Math.atan2",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.atan2()"
      },
      {
        "label": "Math.exp",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.exp()"
      },
      {
        "label": "Math.log",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.log()"
      },
      {
        "label": "Math.pow",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.pow()"
      },
      {
        "label": "Math.random",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.random()"
      },
      {
        "label": "Math.imul",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.imul()"
      },
      {
        "label": "Math.clz32",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.clz32()"
      },
      {
        "label": "Math.fround",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.fround()"
      },
      {
        "label": "Math.trunc",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.trunc()"
      },
      {
        "label": "Math.log2",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.log2()"
      },
      {
        "label": "Math.log10",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Math.log10()"
      }
    ]
  },
  "Number": {
    "methods": [
      {
        "label": "Number.parseInt",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Number.parseInt()"
      },
      {
        "label": "Number.parseFloat",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Number.parseFloat()"
      }
    ]
  },
  "Object": {
    "methods": [
      {
        "label": "Object.defineProperty",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.defineProperty()"
      },
      {
        "label": "Object.getPrototypeOf",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.getPrototypeOf()"
      },
      {
        "label": "Object.setPrototypeOf",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.setPrototypeOf()"
      },
      {
        "label": "Object.create",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.create()"
      },
      {
        "label": "Object.keys",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.keys()"
      },
      {
        "label": "Object.freeze",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.freeze()"
      },
      {
        "label": "Object.seal",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.seal()"
      },
      {
        "label": "Object.preventExtensions",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.preventExtensions()"
      },
      {
        "label": "Object.isFrozen",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.isFrozen()"
      },
      {
        "label": "Object.isSealed",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.isSealed()"
      },
      {
        "label": "Object.isExtensible",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "Object.isExtensible()"
      }
    ]
  },
  "String": {
    "methods": [
      {
        "label": "String.fromCharCode",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "String.fromCharCode()"
      },
      {
        "label": "String.fromCodePoint",
        "type": "method",
        "detail": "() → any",
        "info": "",
        "apply": "String.fromCodePoint()"
      }
    ]
  },
  "fb": {
    "methods": [
      {
        "label": "fb.main.display.create",
        "type": "method",
        "detail": "(type: string) → number",
        "info": "Create a display item",
        "apply": "fb.main.display.create(${1:type})"
      },
      {
        "label": "fb.main.display.remove",
        "type": "method",
        "detail": "(id: number) → boolean",
        "info": "Remove a display item",
        "apply": "fb.main.display.remove(${1:id})"
      },
      {
        "label": "fb.main.display.count",
        "type": "method",
        "detail": "() → number",
        "info": "Get number of display items",
        "apply": "fb.main.display.count()"
      },
      {
        "label": "fb.main.display.get",
        "type": "method",
        "detail": "(id: number) → object",
        "info": "Get display item by ID",
        "apply": "fb.main.display.get(${1:id})"
      },
      {
        "label": "fb.main.display.getAll",
        "type": "method",
        "detail": "() → Array",
        "info": "Get all display items",
        "apply": "fb.main.display.getAll()"
      },
      {
        "label": "fb.main.display.setLabel",
        "type": "method",
        "detail": "(id: number, label: string) → void",
        "info": "Set item label",
        "apply": "fb.main.display.setLabel(${1:id}, ${2:label})"
      },
      {
        "label": "fb.main.display.setValue",
        "type": "method",
        "detail": "(id: number, value: any) → void",
        "info": "Set item value",
        "apply": "fb.main.display.setValue(${1:id}, ${2:value})"
      },
      {
        "label": "fb.main.display.setUnit",
        "type": "method",
        "detail": "(id: number, unit: string) → void",
        "info": "Set item unit",
        "apply": "fb.main.display.setUnit(${1:id}, ${2:unit})"
      },
      {
        "label": "fb.main.display.setPosition",
        "type": "method",
        "detail": "(id: number, x: number, y: number) → void",
        "info": "Set item position",
        "apply": "fb.main.display.setPosition(${1:id}, ${2:x}, ${3:y})"
      },
      {
        "label": "fb.main.display.setPage",
        "type": "method",
        "detail": "(id: number, page: number) → void",
        "info": "Set item page",
        "apply": "fb.main.display.setPage(${1:id}, ${2:page})"
      },
      {
        "label": "fb.main.left.led.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.main.left.led.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.main.left.led.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.main.left.led.on()"
      },
      {
        "label": "fb.main.left.led.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.main.left.led.off()"
      },
      {
        "label": "fb.main.left.backLed.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.main.left.backLed.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.main.left.backLed.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.main.left.backLed.on()"
      },
      {
        "label": "fb.main.left.backLed.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.main.left.backLed.off()"
      },
      {
        "label": "fb.main.left.button.onClick",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register click handler",
        "apply": "fb.main.left.button.onClick(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.main.left.button.onLongPress",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register long press handler",
        "apply": "fb.main.left.button.onLongPress(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.main.left.button.onRelease",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register release handler",
        "apply": "fb.main.left.button.onRelease(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.main.left.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label",
        "apply": "fb.main.left.display.setLabel(${1:text})"
      },
      {
        "label": "fb.main.left.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label",
        "apply": "fb.main.left.display.setTopLabel(${1:text})"
      },
      {
        "label": "fb.main.right.led.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.main.right.led.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.main.right.led.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.main.right.led.on()"
      },
      {
        "label": "fb.main.right.led.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.main.right.led.off()"
      },
      {
        "label": "fb.main.right.backLed.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.main.right.backLed.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.main.right.backLed.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.main.right.backLed.on()"
      },
      {
        "label": "fb.main.right.backLed.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.main.right.backLed.off()"
      },
      {
        "label": "fb.main.right.button.onClick",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register click handler",
        "apply": "fb.main.right.button.onClick(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.main.right.button.onLongPress",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register long press handler",
        "apply": "fb.main.right.button.onLongPress(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.main.right.button.onRelease",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register release handler",
        "apply": "fb.main.right.button.onRelease(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.main.right.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label",
        "apply": "fb.main.right.display.setLabel(${1:text})"
      },
      {
        "label": "fb.main.right.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label",
        "apply": "fb.main.right.display.setTopLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].left.led.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.bars[n].left.led.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.bars[n].left.led.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.bars[n].left.led.on()"
      },
      {
        "label": "fb.bars[n].left.led.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.bars[n].left.led.off()"
      },
      {
        "label": "fb.bars[n].left.backLed.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.bars[n].left.backLed.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.bars[n].left.backLed.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.bars[n].left.backLed.on()"
      },
      {
        "label": "fb.bars[n].left.backLed.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.bars[n].left.backLed.off()"
      },
      {
        "label": "fb.bars[n].left.button.onClick",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register click handler",
        "apply": "fb.bars[n].left.button.onClick(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.bars[n].left.button.onLongPress",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register long press handler",
        "apply": "fb.bars[n].left.button.onLongPress(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.bars[n].left.button.onRelease",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register release handler",
        "apply": "fb.bars[n].left.button.onRelease(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.bars[n].left.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label",
        "apply": "fb.bars[n].left.display.setLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].left.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label",
        "apply": "fb.bars[n].left.display.setTopLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].right.led.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.bars[n].right.led.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.bars[n].right.led.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.bars[n].right.led.on()"
      },
      {
        "label": "fb.bars[n].right.led.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.bars[n].right.led.off()"
      },
      {
        "label": "fb.bars[n].right.backLed.setColor",
        "type": "method",
        "detail": "(r: number, g: number, b: number) → void",
        "info": "Set LED RGB color",
        "apply": "fb.bars[n].right.backLed.setColor(${1:r}, ${2:g}, ${3:b})"
      },
      {
        "label": "fb.bars[n].right.backLed.on",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED on (white)",
        "apply": "fb.bars[n].right.backLed.on()"
      },
      {
        "label": "fb.bars[n].right.backLed.off",
        "type": "method",
        "detail": "() → void",
        "info": "Turn LED off",
        "apply": "fb.bars[n].right.backLed.off()"
      },
      {
        "label": "fb.bars[n].right.button.onClick",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register click handler",
        "apply": "fb.bars[n].right.button.onClick(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.bars[n].right.button.onLongPress",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register long press handler",
        "apply": "fb.bars[n].right.button.onLongPress(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.bars[n].right.button.onRelease",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Register release handler",
        "apply": "fb.bars[n].right.button.onRelease(function() {\n\t${1}\n})"
      },
      {
        "label": "fb.bars[n].right.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label",
        "apply": "fb.bars[n].right.display.setLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].right.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label",
        "apply": "fb.bars[n].right.display.setTopLabel(${1:text})"
      }
    ]
  }
};

export const globalCompletions: CompletionItem[] = [
  {
    "label": "parseInt",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "parseInt()"
  },
  {
    "label": "parseFloat",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "parseFloat()"
  },
  {
    "label": "eval",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "eval()"
  },
  {
    "label": "isNaN",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "isNaN()"
  },
  {
    "label": "isFinite",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "isFinite()"
  },
  {
    "label": "print",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "print()"
  },
  {
    "label": "gc",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "gc()"
  },
  {
    "label": "load",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "load()"
  },
  {
    "label": "loadMapped",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "loadMapped()"
  },
  {
    "label": "loadUserBytecode",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "loadUserBytecode()"
  },
  {
    "label": "setTimeout",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "setTimeout()"
  },
  {
    "label": "clearTimeout",
    "type": "function",
    "detail": "() → any",
    "info": "",
    "apply": "clearTimeout()"
  },
  {
    "label": "throw_error",
    "type": "function",
    "detail": "(msg: string) → never",
    "info": "Throws an error with the given message (test utility)",
    "apply": "throw_error(${1:msg})"
  },
  {
    "label": "assert",
    "type": "function",
    "detail": "(actual: any, expected: any, message: string) → void",
    "info": "Asserts that actual equals expected, throws error if not (test utility)",
    "apply": "assert(${1:actual}, ${2:expected}, ${3:message})"
  },
  {
    "label": "assert_throws",
    "type": "function",
    "detail": "(expected_error: any, func: Function) → void",
    "info": "Asserts that a function throws the expected error type (test utility)",
    "apply": "assert_throws(${1:expected_error}, function() {\n\t${2}\n})"
  }
];
