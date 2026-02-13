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
  "button": {
    "methods": [
      {
        "label": "button.count",
        "type": "method",
        "detail": "() → number",
        "info": "Get number of available buttons",
        "apply": "button.count()"
      },
      {
        "label": "button.setLabel",
        "type": "method",
        "detail": "(position: number, text: string) → void",
        "info": "Set button label text",
        "apply": "button.setLabel(${1:position}, ${2:text})"
      },
      {
        "label": "button.setTopLabel",
        "type": "method",
        "detail": "(position: number, text: string) → void",
        "info": "Set button top label text",
        "apply": "button.setTopLabel(${1:position}, ${2:text})"
      },
      {
        "label": "button.onClick",
        "type": "method",
        "detail": "(position: number, callback: Function) → void",
        "info": "Register click event handler for button",
        "apply": "button.onClick(${1:position}, function() {\n\t${2}\n})"
      },
      {
        "label": "button.onLongPress",
        "type": "method",
        "detail": "(position: number, callback: Function) → void",
        "info": "Register long press event handler for button",
        "apply": "button.onLongPress(${1:position}, function() {\n\t${2}\n})"
      },
      {
        "label": "button.onRelease",
        "type": "method",
        "detail": "(position: number, callback: Function) → void",
        "info": "Register release event handler for button",
        "apply": "button.onRelease(${1:position}, function() {\n\t${2}\n})"
      }
    ]
  },
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
  "display": {
    "methods": [
      {
        "label": "display.create",
        "type": "method",
        "detail": "(x: number, y: number, width: number, boxType: number, fontSize: number, alignment: number, label: string, unit: string, round: number, page: number) → number",
        "info": "Create a new display item on the screen",
        "apply": "display.create(${1:x}, ${2:y}, ${3:width}, ${4:boxType}, ${5:fontSize}, ${6:alignment}, ${7:label}, ${8:unit}, ${9:round}, ${10:page})"
      },
      {
        "label": "display.remove",
        "type": "method",
        "detail": "(id: number) → boolean",
        "info": "Remove a display item by ID",
        "apply": "display.remove(${1:id})"
      },
      {
        "label": "display.count",
        "type": "method",
        "detail": "() → number",
        "info": "Get number of display items",
        "apply": "display.count()"
      },
      {
        "label": "display.get",
        "type": "method",
        "detail": "(id: number) → object",
        "info": "Get display item information by ID",
        "apply": "display.get(${1:id})"
      },
      {
        "label": "display.getAll",
        "type": "method",
        "detail": "() → number[]",
        "info": "Get array of all display item IDs",
        "apply": "display.getAll()"
      },
      {
        "label": "display.setLabel",
        "type": "method",
        "detail": "(id: number, label: string) → boolean",
        "info": "Set display item label text",
        "apply": "display.setLabel(${1:id}, ${2:label})"
      },
      {
        "label": "display.setValue",
        "type": "method",
        "detail": "(id: number, value: string) → boolean",
        "info": "Set display item value text",
        "apply": "display.setValue(${1:id}, ${2:value})"
      },
      {
        "label": "display.setUnit",
        "type": "method",
        "detail": "(id: number, unit: string) → boolean",
        "info": "Set display item unit text",
        "apply": "display.setUnit(${1:id}, ${2:unit})"
      },
      {
        "label": "display.setPosition",
        "type": "method",
        "detail": "(id: number, x: number, y: number) → boolean",
        "info": "Set display item position",
        "apply": "display.setPosition(${1:id}, ${2:x}, ${3:y})"
      },
      {
        "label": "display.setPage",
        "type": "method",
        "detail": "(id: number, page: number) → boolean",
        "info": "Set display item page",
        "apply": "display.setPage(${1:id}, ${2:page})"
      }
    ]
  },
  "led": {
    "methods": [
      {
        "label": "led.count",
        "type": "method",
        "detail": "() → number",
        "info": "Get number of available LEDs",
        "apply": "led.count()"
      },
      {
        "label": "led.on",
        "type": "method",
        "detail": "(position: number) → void",
        "info": "Turn LED white at specified position",
        "apply": "led.on(${1:position})"
      },
      {
        "label": "led.off",
        "type": "method",
        "detail": "(position: number) → void",
        "info": "Turn LED off at specified position",
        "apply": "led.off(${1:position})"
      },
      {
        "label": "led.setColor",
        "type": "method",
        "detail": "(position: number, r: number, g: number, b: number) → void",
        "info": "Set LED RGB color (0-255 for each component)",
        "apply": "led.setColor(${1:position}, ${2:r}, ${3:g}, ${4:b})"
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
        "label": "sensor.getAll",
        "type": "method",
        "detail": "() → object[]",
        "info": "Get array of all sensor information objects",
        "apply": "sensor.getAll()"
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
