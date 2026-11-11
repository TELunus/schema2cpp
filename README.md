# schema2cpp

Generate C++17 classes from a JSON Schema. JSON is read with
[Wuffs](https://github.com/google/wuffs), and the generated constructors throw if
the JSON is malformed or does not match the schema.

```
schema2cpp schema.json [-o out.hpp] [--name Root] [--namespace ns]
                       [--data data.json [--data-name accessor]]
```

## Two ways to use the generated header

**Load JSON at run time** (schema fixed at build time):

```cpp
#include "person.hpp"
try { Person p(json_text); }
catch (const s2c::ParseError& e)      { /* malformed JSON */ }
catch (const s2c::ValidationError& e) { /* e.g. "$.address.zip: missing required property" */ }
```

**Embed a data file** (`--data`): the file is checked against the schema while the
header is generated (the build fails if it does not match) and compiled in as C++
code. No parsing happens at run time, and the program does not need the parser:

```cpp
#include "config.hpp"
const Person& p = config();
```

## Updating an existing JSON document

Every generated class has `update(json_text, indent = -1)`: it parses an existing JSON
object, writes the class's members into it, and returns the new text. Anything the class
does not model is kept.

```cpp
Person p(json_text);
p.age = 31;
std::string newer = p.update(old_json_text);   // or p.update(old_json_text, 2) to pretty-print
```

- Keys keep the order they had in the old document; new keys are appended in schema order.
- Keys not in the schema are preserved, at every nesting level, unless that object has
  `additionalProperties: false` (then they are dropped, so the output stays valid).
- Nested objects are merged. Array elements are merged with the element at the same index,
  so extra keys follow edits made in place (inserting or removing elements shifts them).
  Free-form values (`{}` or `"type": "object"` without `properties`) are replaced whole.
- An unset optional member removes its key. For an optional member that is also nullable,
  unset becomes `null` if the key was already present and stays absent otherwise.
- The old document need not match the schema; the new one always does. `update` throws
  `s2c::ValidationError` if the members you set violate the schema (e.g. `age = 200`), and
  `s2c::ParseError` / `ValidationError` if the old text is not a JSON object.
- Whitespace and number spelling are not preserved (`1.0` is written back as `1`).
- `update` and `to_value` are reserved: a schema property with either name becomes
  `update_` / `to_value_`.
- `update` needs the parser (`s2c_runtime`), like the `json_text` constructor.

## Using it from Meson

Copy `examples/consumer/subprojects/schema2cpp.wrap` into your `subprojects/`
(set the URL and a release tag), then see `examples/consumer/meson.build`.
Use `dependency("s2c_runtime")` to parse JSON at run time and
`dependency("s2c_headers")` for embedded data only.

## Schema support

Types `string`, `integer`, `number`, `boolean`, `null`, `array`, `object`;
`["T","null"]` (becomes `std::optional<T>`); non-required properties
(`std::optional<T>`); nested objects and local `$ref` (`$defs`/`definitions`);
`required`; `additionalProperties: false`; string `enum`; `minimum`/`maximum`;
`minLength`/`maxLength`; `minItems`/`maxItems`. Other keywords (`pattern`, `format`,
`oneOf`/`anyOf`/`allOf`, `exclusiveMinimum`, ...) are ignored with a warning.
Recursive schemas, union types and non-string enums are rejected.

## Building

Build with meson
```
meson setup buildDir
meson test -C buildDir
```
