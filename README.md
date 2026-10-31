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

Ensure you've got the `third_party/wuffs` submodule:
```
git submodule update --init
```
Then build with meson
```
meson setup buildDir
meson test -C buildDir
```
