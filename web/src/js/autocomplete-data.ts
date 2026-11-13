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
        "detail": "(args: any) → void",
        "info": "Outputs a message to the console",
        "apply": "console.log(${1:args})"
      }
    ]
  },
  "mqtt": {
    "methods": [
      {
        "label": "mqtt.getBrokerCount",
        "type": "method",
        "detail": "() → number",
        "info": "Get number of configured MQTT brokers",
        "apply": "mqtt.getBrokerCount()"
      },
      {
        "label": "mqtt.getBrokerName",
        "type": "method",
        "detail": "(brokerId: number) → string",
        "info": "Get MQTT broker name",
        "apply": "mqtt.getBrokerName(${1:brokerId})"
      },
      {
        "label": "mqtt.isConnected",
        "type": "method",
        "detail": "(brokerId: number) → boolean",
        "info": "Check if MQTT broker is connected",
        "apply": "mqtt.isConnected(${1:brokerId})"
      },
      {
        "label": "mqtt.publish",
        "type": "method",
        "detail": "(brokerId: number, topic: string, payload: string, qos: number, retain: number) → void",
        "info": "Publish MQTT message to topic",
        "apply": "mqtt.publish(${1:brokerId}, ${2:topic}, ${3:payload}, ${4:qos}, ${5:retain})"
      },
      {
        "label": "mqtt.subscribe",
        "type": "method",
        "detail": "(brokerId: number, topic: string, callback: Function, qos: number) → void",
        "info": "Subscribe to MQTT topic with callback",
        "apply": "mqtt.subscribe(${1:brokerId}, ${2:topic}, function() {\n\t${3}\n}, ${4:qos})"
      },
      {
        "label": "mqtt.unsubscribe",
        "type": "method",
        "detail": "(brokerId: number, topic: string) → void",
        "info": "Unsubscribe from MQTT topic",
        "apply": "mqtt.unsubscribe(${1:brokerId}, ${2:topic})"
      },
      {
        "label": "mqtt.onConnect",
        "type": "method",
        "detail": "(brokerId: number, callback: Function) → void",
        "info": "Register callback for MQTT broker connection event",
        "apply": "mqtt.onConnect(${1:brokerId}, function() {\n\t${2}\n})"
      },
      {
        "label": "mqtt.onDisconnect",
        "type": "method",
        "detail": "(brokerId: number, callback: Function) → void",
        "info": "Register callback for MQTT broker disconnection event",
        "apply": "mqtt.onDisconnect(${1:brokerId}, function() {\n\t${2}\n})"
      }
    ]
  },
  "performance": {
    "methods": [
      {
        "label": "performance.now",
        "type": "method",
        "detail": "() → number",
        "info": "Returns a high-resolution timestamp in milliseconds",
        "apply": "performance.now()"
      }
    ]
  },
  "sensor": {
    "methods": [
      {
        "label": "sensor.count",
        "type": "method",
        "detail": "() → number",
        "info": "Get number of available sensors",
        "apply": "sensor.count()"
      },
      {
        "label": "sensor.getValue",
        "type": "method",
        "detail": "(sensorId: number) → number",
        "info": "Read current sensor value",
        "apply": "sensor.getValue(${1:sensorId})"
      },
      {
        "label": "sensor.getType",
        "type": "method",
        "detail": "(sensorId: number) → string",
        "info": "Get sensor type name",
        "apply": "sensor.getType(${1:sensorId})"
      },
      {
        "label": "sensor.getInfo",
        "type": "method",
        "detail": "(sensorId: number) → object",
        "info": "Get sensor information object",
        "apply": "sensor.getInfo(${1:sensorId})"
      },
      {
        "label": "sensor.forEach",
        "type": "method",
        "detail": "(callback: Function) → void",
        "info": "Iterate over every sensor, invoking callback(id, type) once per sensor. Memory-safe alternative to a getAll() array: it allocates nothing up front (important on the small script heap). Use sensor.getValue(id) / sensor.getInfo(id) inside the callback if you need more than id + type.",
        "apply": "sensor.forEach(function() {\n\t${1}\n})"
      },
      {
        "label": "sensor.onChange",
        "type": "method",
        "detail": "(sensorId: number, callback: Function) → void",
        "info": "Register change event handler for sensor value changes",
        "apply": "sensor.onChange(${1:sensorId}, function() {\n\t${2}\n})"
      }
    ]
  },
  "system": {
    "methods": [
      {
        "label": "system.uptime",
        "type": "method",
        "detail": "() → number",
        "info": "Get device uptime in seconds",
        "apply": "system.uptime()"
      },
      {
        "label": "system.freeHeap",
        "type": "method",
        "detail": "() → number",
        "info": "Get free heap memory in bytes",
        "apply": "system.freeHeap()"
      },
      {
        "label": "system.version",
        "type": "method",
        "detail": "() → string",
        "info": "Get firmware version string",
        "apply": "system.version()"
      }
    ]
  },
  "Array": {
    "methods": [
      {
        "label": "Array.isArray",
        "type": "method",
        "detail": "(value: any) → boolean",
        "info": "Determines whether the passed value is an Array",
        "apply": "Array.isArray(${1:value})"
      }
    ]
  },
  "Date": {
    "methods": [
      {
        "label": "Date.now",
        "type": "method",
        "detail": "() → number",
        "info": "Returns the number of milliseconds elapsed since January 1, 1970",
        "apply": "Date.now()"
      }
    ]
  },
  "JSON": {
    "methods": [
      {
        "label": "JSON.parse",
        "type": "method",
        "detail": "(text: string, reviver: Function) → any",
        "info": "Parses a JSON string, constructing the JavaScript value or object",
        "apply": "JSON.parse(${1:text}, function() {\n\t${2}\n})"
      },
      {
        "label": "JSON.stringify",
        "type": "method",
        "detail": "(value: any, replacer: Function|any[], space: string|number) → string",
        "info": "Converts a JavaScript value to a JSON string",
        "apply": "JSON.stringify(${1:value}, ${2:replacer}, ${3:space})"
      }
    ]
  },
  "Math": {
    "methods": [
      {
        "label": "Math.min",
        "type": "method",
        "detail": "(value: number) → number",
        "info": "Returns the smallest of zero or more numbers",
        "apply": "Math.min(${1:value})"
      },
      {
        "label": "Math.max",
        "type": "method",
        "detail": "(value: number) → number",
        "info": "Returns the largest of zero or more numbers",
        "apply": "Math.max(${1:value})"
      },
      {
        "label": "Math.sign",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the sign of a number",
        "apply": "Math.sign(${1:x})"
      },
      {
        "label": "Math.abs",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the absolute value of a number",
        "apply": "Math.abs(${1:x})"
      },
      {
        "label": "Math.floor",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the largest integer less than or equal to a number",
        "apply": "Math.floor(${1:x})"
      },
      {
        "label": "Math.ceil",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the smallest integer greater than or equal to a number",
        "apply": "Math.ceil(${1:x})"
      },
      {
        "label": "Math.round",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the value of a number rounded to the nearest integer",
        "apply": "Math.round(${1:x})"
      },
      {
        "label": "Math.sqrt",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the square root of a number",
        "apply": "Math.sqrt(${1:x})"
      },
      {
        "label": "Math.sin",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the sine of a number",
        "apply": "Math.sin(${1:x})"
      },
      {
        "label": "Math.cos",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the cosine of a number",
        "apply": "Math.cos(${1:x})"
      },
      {
        "label": "Math.tan",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the tangent of a number",
        "apply": "Math.tan(${1:x})"
      },
      {
        "label": "Math.asin",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the arcsine of a number",
        "apply": "Math.asin(${1:x})"
      },
      {
        "label": "Math.acos",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the arccosine of a number",
        "apply": "Math.acos(${1:x})"
      },
      {
        "label": "Math.atan",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the arctangent of a number",
        "apply": "Math.atan(${1:x})"
      },
      {
        "label": "Math.atan2",
        "type": "method",
        "detail": "(y: number, x: number) → number",
        "info": "Returns the arctangent of the quotient of its arguments",
        "apply": "Math.atan2(${1:y}, ${2:x})"
      },
      {
        "label": "Math.exp",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns E raised to the power of a number",
        "apply": "Math.exp(${1:x})"
      },
      {
        "label": "Math.log",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the natural logarithm of a number",
        "apply": "Math.log(${1:x})"
      },
      {
        "label": "Math.pow",
        "type": "method",
        "detail": "(base: number, exponent: number) → number",
        "info": "Returns base to the exponent power",
        "apply": "Math.pow(${1:base}, ${2:exponent})"
      },
      {
        "label": "Math.random",
        "type": "method",
        "detail": "() → number",
        "info": "Returns a pseudo-random number between 0 and 1",
        "apply": "Math.random()"
      },
      {
        "label": "Math.imul",
        "type": "method",
        "detail": "(a: number, b: number) → number",
        "info": "Returns the result of the 32-bit integer multiplication",
        "apply": "Math.imul(${1:a}, ${2:b})"
      },
      {
        "label": "Math.clz32",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the number of leading zero bits in the 32-bit binary representation",
        "apply": "Math.clz32(${1:x})"
      },
      {
        "label": "Math.fround",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the nearest 32-bit single precision float representation",
        "apply": "Math.fround(${1:x})"
      },
      {
        "label": "Math.trunc",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the integer part of a number",
        "apply": "Math.trunc(${1:x})"
      },
      {
        "label": "Math.log2",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the base 2 logarithm of a number",
        "apply": "Math.log2(${1:x})"
      },
      {
        "label": "Math.log10",
        "type": "method",
        "detail": "(x: number) → number",
        "info": "Returns the base 10 logarithm of a number",
        "apply": "Math.log10(${1:x})"
      }
    ]
  },
  "Number": {
    "methods": [
      {
        "label": "Number.parseInt",
        "type": "method",
        "detail": "(string: string, radix: number) → number",
        "info": "Parses a string argument and returns an integer",
        "apply": "Number.parseInt(${1:string}, ${2:radix})"
      },
      {
        "label": "Number.parseFloat",
        "type": "method",
        "detail": "(string: string) → number",
        "info": "Parses a string argument and returns a floating point number",
        "apply": "Number.parseFloat(${1:string})"
      }
    ]
  },
  "Object": {
    "methods": [
      {
        "label": "Object.defineProperty",
        "type": "method",
        "detail": "(obj: object, prop: string, descriptor: object) → object",
        "info": "Defines a new property or modifies an existing property on an object",
        "apply": "Object.defineProperty(${1:obj}, ${2:prop}, ${3:descriptor})"
      },
      {
        "label": "Object.getPrototypeOf",
        "type": "method",
        "detail": "(obj: object) → object",
        "info": "Returns the prototype of the specified object",
        "apply": "Object.getPrototypeOf(${1:obj})"
      },
      {
        "label": "Object.setPrototypeOf",
        "type": "method",
        "detail": "(obj: object, prototype: object) → object",
        "info": "Sets the prototype of a specified object",
        "apply": "Object.setPrototypeOf(${1:obj}, ${2:prototype})"
      },
      {
        "label": "Object.create",
        "type": "method",
        "detail": "(proto: object) → object",
        "info": "Creates a new object with the specified prototype object",
        "apply": "Object.create(${1:proto})"
      },
      {
        "label": "Object.keys",
        "type": "method",
        "detail": "(obj: object) → string[]",
        "info": "Returns an array of a given object's own enumerable property names",
        "apply": "Object.keys(${1:obj})"
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
        "detail": "(charCode: number) → string",
        "info": "Returns a string created from the specified sequence of UTF-16 code units",
        "apply": "String.fromCharCode(${1:charCode})"
      },
      {
        "label": "String.fromCodePoint",
        "type": "method",
        "detail": "(codePoint: number) → string",
        "info": "Returns a string created from the specified sequence of code points",
        "apply": "String.fromCodePoint(${1:codePoint})"
      }
    ]
  },
  "fb": {
    "methods": [
      {
        "label": "fb.key",
        "type": "method",
        "detail": "(name: string) → FbSide | FbMainSide",
        "info": "Key by its \"name\" in config.json: the same object as fb.main.left/right (FbMainSide) or fb.bars[i].left/right (FbSide). Throws ReferenceError for an unknown name.",
        "apply": "fb.key(${1:name})"
      },
      {
        "label": "fb.main.display.create",
        "type": "method",
        "detail": "(x: number, y: number, width: number, boxType: number, fontSize: number, alignment: number, label: string, unit: string, round: number, page: number) → number",
        "info": "Create a display item; returns its id",
        "apply": "fb.main.display.create(${1:x}, ${2:y}, ${3:width}, ${4:boxType}, ${5:fontSize}, ${6:alignment}, ${7:label}, ${8:unit}, ${9:round}, ${10:page})"
      },
      {
        "label": "fb.main.display.remove",
        "type": "method",
        "detail": "(idOrName: number | string) → boolean",
        "info": "Remove a display item (by id or config.json name)",
        "apply": "fb.main.display.remove(${1:idOrName})"
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
        "detail": "(idOrName: number | string) → object",
        "info": "Get a display item by id or config.json name",
        "apply": "fb.main.display.get(${1:idOrName})"
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
        "detail": "(idOrName: number | string, label: string) → void",
        "info": "Set item label (item by id or config.json name)",
        "apply": "fb.main.display.setLabel(${1:idOrName}, ${2:label})"
      },
      {
        "label": "fb.main.display.setValue",
        "type": "method",
        "detail": "(idOrName: number | string, value: any) → void",
        "info": "Set item value (item by id or config.json name)",
        "apply": "fb.main.display.setValue(${1:idOrName}, ${2:value})"
      },
      {
        "label": "fb.main.display.setUnit",
        "type": "method",
        "detail": "(idOrName: number | string, unit: string) → void",
        "info": "Set item unit (item by id or config.json name)",
        "apply": "fb.main.display.setUnit(${1:idOrName}, ${2:unit})"
      },
      {
        "label": "fb.main.display.setPosition",
        "type": "method",
        "detail": "(idOrName: number | string, x: number, y: number) → void",
        "info": "Set item position (item by id or config.json name)",
        "apply": "fb.main.display.setPosition(${1:idOrName}, ${2:x}, ${3:y})"
      },
      {
        "label": "fb.main.display.setPage",
        "type": "method",
        "detail": "(idOrName: number | string, page: number) → void",
        "info": "Set item page, 0 = every page (item by id or config.json name)",
        "apply": "fb.main.display.setPage(${1:idOrName}, ${2:page})"
      },
      {
        "label": "fb.main.display.showPage",
        "type": "method",
        "detail": "(page: number) → void",
        "info": "Show a page of the main display: items and the clock on this page or on page 0 (every page) are shown, the others hidden. Page 1 is shown at boot.",
        "apply": "fb.main.display.showPage(${1:page})"
      },
      {
        "label": "fb.main.display.getShownPage",
        "type": "method",
        "detail": "() → number",
        "info": "Page of the main display shown now",
        "apply": "fb.main.display.getShownPage()"
      },
      {
        "label": "fb.main.display.setIcon",
        "type": "method",
        "detail": "(idOrName: number | string, icon: string) → boolean",
        "info": "Show an icon from config.json \"icons\" before the value (\"\" hides it; item by id or config.json name); false when there is no such item",
        "apply": "fb.main.display.setIcon(${1:idOrName}, ${2:icon})"
      },
      {
        "label": "fb.main.display.setFont",
        "type": "method",
        "detail": "(idOrName: number | string, font: string | number, size: number) → boolean",
        "info": "Set the font of the value and unit (item by id or config.json name; the caption keeps its own; font \"\" restores config.json); false when there is no such item",
        "apply": "fb.main.display.setFont(${1:idOrName}, ${2:font}, ${3:size})"
      },
      {
        "label": "fb.main.left.button.onClick",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register click handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.main.left.button.onClick(${1:callback})"
      },
      {
        "label": "fb.main.left.button.onLongPress",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register long press handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.main.left.button.onLongPress(${1:callback})"
      },
      {
        "label": "fb.main.left.button.onRelease",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register release handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.main.left.button.onRelease(${1:callback})"
      },
      {
        "label": "fb.main.left.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label. Text markup applies: \\\\cRRGGBB colour, \\\\s{px} size, \\\\f{font} font, \\\\al \\\\ac \\\\ar alignment (write the backslash twice in a JS string); at most 90 bytes",
        "apply": "fb.main.left.display.setLabel(${1:text})"
      },
      {
        "label": "fb.main.left.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label. Text markup applies as for setLabel; at most 90 bytes",
        "apply": "fb.main.left.display.setTopLabel(${1:text})"
      },
      {
        "label": "fb.main.left.display.getLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Label currently shown (last setLabel / MQTT text, else the config label)",
        "apply": "fb.main.left.display.getLabel()"
      },
      {
        "label": "fb.main.left.display.getTopLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Top label currently shown (last setTopLabel / MQTT text, else the config label)",
        "apply": "fb.main.left.display.getTopLabel()"
      },
      {
        "label": "fb.main.left.display.setIcon",
        "type": "method",
        "detail": "(icon: string) → void",
        "info": "Show an icon from config.json \"icons\" instead of the bottom label (\"\" shows the label again)",
        "apply": "fb.main.left.display.setIcon(${1:icon})"
      },
      {
        "label": "fb.main.left.display.setFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the bottom label (default: the style's key label font, config.json \"style.key.label\"; font \"\" restores config.json)",
        "apply": "fb.main.left.display.setFont(${1:font}, ${2:size})"
      },
      {
        "label": "fb.main.left.display.setTopFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the top label (default: the style's key top label font, config.json \"style.key.toplabel\"; font \"\" restores config.json)",
        "apply": "fb.main.left.display.setTopFont(${1:font}, ${2:size})"
      },
      {
        "label": "fb.main.right.button.onClick",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register click handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.main.right.button.onClick(${1:callback})"
      },
      {
        "label": "fb.main.right.button.onLongPress",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register long press handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.main.right.button.onLongPress(${1:callback})"
      },
      {
        "label": "fb.main.right.button.onRelease",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register release handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.main.right.button.onRelease(${1:callback})"
      },
      {
        "label": "fb.main.right.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label. Text markup applies: \\\\cRRGGBB colour, \\\\s{px} size, \\\\f{font} font, \\\\al \\\\ac \\\\ar alignment (write the backslash twice in a JS string); at most 90 bytes",
        "apply": "fb.main.right.display.setLabel(${1:text})"
      },
      {
        "label": "fb.main.right.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label. Text markup applies as for setLabel; at most 90 bytes",
        "apply": "fb.main.right.display.setTopLabel(${1:text})"
      },
      {
        "label": "fb.main.right.display.getLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Label currently shown (last setLabel / MQTT text, else the config label)",
        "apply": "fb.main.right.display.getLabel()"
      },
      {
        "label": "fb.main.right.display.getTopLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Top label currently shown (last setTopLabel / MQTT text, else the config label)",
        "apply": "fb.main.right.display.getTopLabel()"
      },
      {
        "label": "fb.main.right.display.setIcon",
        "type": "method",
        "detail": "(icon: string) → void",
        "info": "Show an icon from config.json \"icons\" instead of the bottom label (\"\" shows the label again)",
        "apply": "fb.main.right.display.setIcon(${1:icon})"
      },
      {
        "label": "fb.main.right.display.setFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the bottom label (default: the style's key label font, config.json \"style.key.label\"; font \"\" restores config.json)",
        "apply": "fb.main.right.display.setFont(${1:font}, ${2:size})"
      },
      {
        "label": "fb.main.right.display.setTopFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the top label (default: the style's key top label font, config.json \"style.key.toplabel\"; font \"\" restores config.json)",
        "apply": "fb.main.right.display.setTopFont(${1:font}, ${2:size})"
      },
      {
        "label": "fb.main.list.next",
        "type": "method",
        "detail": "(nameOrIndex: string | number) → number",
        "info": "Select the next item (one down; stops at the last, no wrap); returns the new index",
        "apply": "fb.main.list.next(${1:nameOrIndex})"
      },
      {
        "label": "fb.main.list.prev",
        "type": "method",
        "detail": "(nameOrIndex: string | number) → number",
        "info": "Select the previous item (one up; stops at the first, no wrap); returns the new index",
        "apply": "fb.main.list.prev(${1:nameOrIndex})"
      },
      {
        "label": "fb.main.list.select",
        "type": "method",
        "detail": "(nameOrIndex: string | number, index: number) → boolean",
        "info": "Select an item by index; false when the index is out of range",
        "apply": "fb.main.list.select(${1:nameOrIndex}, ${2:index})"
      },
      {
        "label": "fb.main.list.getSelected",
        "type": "method",
        "detail": "(nameOrIndex: string | number) → number",
        "info": "Index of the selected item; -1 when the list is empty",
        "apply": "fb.main.list.getSelected(${1:nameOrIndex})"
      },
      {
        "label": "fb.main.list.count",
        "type": "method",
        "detail": "(nameOrIndex: string | number) → number",
        "info": "Number of items in the list",
        "apply": "fb.main.list.count(${1:nameOrIndex})"
      },
      {
        "label": "fb.main.list.setItems",
        "type": "method",
        "detail": "(nameOrIndex: string | number, items: string[]) → boolean",
        "info": "Replace the items (no newlines in an item); the selection is clamped to the new items",
        "apply": "fb.main.list.setItems(${1:nameOrIndex}, ${2:items})"
      },
      {
        "label": "fb.main.list.setFont",
        "type": "method",
        "detail": "(nameOrIndex: string | number, font: string | number, size: number) → void",
        "info": "Set the font of the items (default: the style's list font, config.json \"style.list\", at the list's \"fontsize\"; font \"\" restores config.json)",
        "apply": "fb.main.list.setFont(${1:nameOrIndex}, ${2:font}, ${3:size})"
      },
      {
        "label": "fb.main.image.setIcon",
        "type": "method",
        "detail": "(nameOrIndex: string | number, icon: string) → void",
        "info": "Show an icon from config.json \"icons\" at the image's width x height (\"\" clears the image)",
        "apply": "fb.main.image.setIcon(${1:nameOrIndex}, ${2:icon})"
      },
      {
        "label": "fb.main.image.setSrc",
        "type": "method",
        "detail": "(nameOrIndex: string | number, key: string) → void",
        "info": "Show any key of the device's images bucket (an LVGL .bin raster, e.g. \"radar.bin\"; \"\" clears the image)",
        "apply": "fb.main.image.setSrc(${1:nameOrIndex}, ${2:key})"
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
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register click handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.bars[n].left.button.onClick(${1:callback})"
      },
      {
        "label": "fb.bars[n].left.button.onLongPress",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register long press handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.bars[n].left.button.onLongPress(${1:callback})"
      },
      {
        "label": "fb.bars[n].left.button.onRelease",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register release handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.bars[n].left.button.onRelease(${1:callback})"
      },
      {
        "label": "fb.bars[n].left.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label. Text markup applies: \\\\cRRGGBB colour, \\\\s{px} size, \\\\f{font} font, \\\\al \\\\ac \\\\ar alignment (write the backslash twice in a JS string); at most 90 bytes",
        "apply": "fb.bars[n].left.display.setLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].left.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label. Text markup applies as for setLabel; at most 90 bytes",
        "apply": "fb.bars[n].left.display.setTopLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].left.display.getLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Label currently shown (last setLabel / MQTT text, else the config label)",
        "apply": "fb.bars[n].left.display.getLabel()"
      },
      {
        "label": "fb.bars[n].left.display.getTopLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Top label currently shown (last setTopLabel / MQTT text, else the config label)",
        "apply": "fb.bars[n].left.display.getTopLabel()"
      },
      {
        "label": "fb.bars[n].left.display.setIcon",
        "type": "method",
        "detail": "(icon: string) → void",
        "info": "Show an icon from config.json \"icons\" instead of the bottom label (\"\" shows the label again)",
        "apply": "fb.bars[n].left.display.setIcon(${1:icon})"
      },
      {
        "label": "fb.bars[n].left.display.setFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the bottom label (default: the style's key label font, config.json \"style.key.label\"; font \"\" restores config.json)",
        "apply": "fb.bars[n].left.display.setFont(${1:font}, ${2:size})"
      },
      {
        "label": "fb.bars[n].left.display.setTopFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the top label (default: the style's key top label font, config.json \"style.key.toplabel\"; font \"\" restores config.json)",
        "apply": "fb.bars[n].left.display.setTopFont(${1:font}, ${2:size})"
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
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register click handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.bars[n].right.button.onClick(${1:callback})"
      },
      {
        "label": "fb.bars[n].right.button.onLongPress",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register long press handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.bars[n].right.button.onLongPress(${1:callback})"
      },
      {
        "label": "fb.bars[n].right.button.onRelease",
        "type": "method",
        "detail": "(callback: () => boolean | void) → void",
        "info": "Register release handler. Return false to suppress this press's MQTT publishing (configured topics and the Home Assistant event).",
        "apply": "fb.bars[n].right.button.onRelease(${1:callback})"
      },
      {
        "label": "fb.bars[n].right.display.setLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display label. Text markup applies: \\\\cRRGGBB colour, \\\\s{px} size, \\\\f{font} font, \\\\al \\\\ac \\\\ar alignment (write the backslash twice in a JS string); at most 90 bytes",
        "apply": "fb.bars[n].right.display.setLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].right.display.setTopLabel",
        "type": "method",
        "detail": "(text: string) → void",
        "info": "Set button display top label. Text markup applies as for setLabel; at most 90 bytes",
        "apply": "fb.bars[n].right.display.setTopLabel(${1:text})"
      },
      {
        "label": "fb.bars[n].right.display.getLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Label currently shown (last setLabel / MQTT text, else the config label)",
        "apply": "fb.bars[n].right.display.getLabel()"
      },
      {
        "label": "fb.bars[n].right.display.getTopLabel",
        "type": "method",
        "detail": "() → string",
        "info": "Top label currently shown (last setTopLabel / MQTT text, else the config label)",
        "apply": "fb.bars[n].right.display.getTopLabel()"
      },
      {
        "label": "fb.bars[n].right.display.setIcon",
        "type": "method",
        "detail": "(icon: string) → void",
        "info": "Show an icon from config.json \"icons\" instead of the bottom label (\"\" shows the label again)",
        "apply": "fb.bars[n].right.display.setIcon(${1:icon})"
      },
      {
        "label": "fb.bars[n].right.display.setFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the bottom label (default: the style's key label font, config.json \"style.key.label\"; font \"\" restores config.json)",
        "apply": "fb.bars[n].right.display.setFont(${1:font}, ${2:size})"
      },
      {
        "label": "fb.bars[n].right.display.setTopFont",
        "type": "method",
        "detail": "(font: string | number, size: number) → void",
        "info": "Set the font of the top label (default: the style's key top label font, config.json \"style.key.toplabel\"; font \"\" restores config.json)",
        "apply": "fb.bars[n].right.display.setTopFont(${1:font}, ${2:size})"
      }
    ]
  }
};

export const globalCompletions: CompletionItem[] = [
  {
    "label": "parseInt",
    "type": "function",
    "detail": "(string: string, radix: number) → number",
    "info": "Parses a string argument and returns an integer",
    "apply": "parseInt(${1:string}, ${2:radix})"
  },
  {
    "label": "parseFloat",
    "type": "function",
    "detail": "(string: string) → number",
    "info": "Parses a string argument and returns a floating point number",
    "apply": "parseFloat(${1:string})"
  },
  {
    "label": "eval",
    "type": "function",
    "detail": "(code: string) → any",
    "info": "Evaluates JavaScript code represented as a string",
    "apply": "eval(${1:code})"
  },
  {
    "label": "isNaN",
    "type": "function",
    "detail": "(value: any) → boolean",
    "info": "Determines whether a value is NaN (Not-A-Number)",
    "apply": "isNaN(${1:value})"
  },
  {
    "label": "isFinite",
    "type": "function",
    "detail": "(value: any) → boolean",
    "info": "Determines whether a value is a finite number",
    "apply": "isFinite(${1:value})"
  },
  {
    "label": "print",
    "type": "function",
    "detail": "(args: any) → void",
    "info": "Print values to console output",
    "apply": "print(${1:args})"
  },
  {
    "label": "gc",
    "type": "function",
    "detail": "() → void",
    "info": "Triggers garbage collection (development/testing only)",
    "apply": "gc()"
  },
  {
    "label": "load",
    "type": "function",
    "detail": "(filename: string) → any",
    "info": "Loads and executes a JavaScript file",
    "apply": "load(${1:filename})"
  },
  {
    "label": "loadMapped",
    "type": "function",
    "detail": "(filename: string, offset: number, size: number) → any",
    "info": "Loads a memory-mapped bytecode file for efficient execution",
    "apply": "loadMapped(${1:filename}, ${2:offset}, ${3:size})"
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
    "detail": "(callback: Function, delay: number) → number",
    "info": "Executes a function after a specified delay in milliseconds",
    "apply": "setTimeout(function() {\n\t${1}\n}, ${2:delay})"
  },
  {
    "label": "clearTimeout",
    "type": "function",
    "detail": "(timeoutId: number) → void",
    "info": "Cancels a timeout previously established by setTimeout",
    "apply": "clearTimeout(${1:timeoutId})"
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
