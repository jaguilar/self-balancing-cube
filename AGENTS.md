## General

- Always run `bazel` and `openocd` commands with `BypassSandbox: true` (do not try sandboxed mode first).
- For STM32C5 targets, `target/stm32c5x.cfg` requires `interface/stlink-dap.cfg` with `transport select dapdirect_swd` (do not use `interface/stlink.cfg`, which uses HLA and is rejected). Minimal flash command: `openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" -f target/stm32c5x.cfg -c "program <path.elf> verify reset exit"`.
- Code should be written in C++23. 

## Naming Conventions (Google C++ Style)
- **Files:** `snake_case.h` / `snake_case.cc`
- **Types (classes, structs, enums, aliases):** `PascalCase`
- **Functions & Methods:** `PascalCase` (cheap getters/setters may be `snake_case`)
- **Local variables & parameters:** `snake_case`
- **Class data members:** `snake_case_` (trailing underscore)
- **Struct data members:** `snake_case` (no trailing underscore)
- **Constants & enumerators:** `kPascalCase`
- **Macros:** `UPPER_SNAKE_CASE`
