# Project code rules

## Layout and C++

- Use C++23, two-space indentation and the existing `.cpp` / `.h` convention.
- Format new or substantially edited code with the root `.clang-format` configuration.
- Keep `src/main.cpp` as the entrypoint; application lifecycle belongs in `src/app/`.
- Group code by responsibility: `game/`, `input/`, `settings/`, `ui/` and `testing/`.
- Keep headers beside their implementations. Use source-root include paths across modules.
- Put host APIs in `namespace lollipop`; keep implementation details private to their translation unit.
- Headers must compile independently. Prefer forward declarations to generated or heavyweight includes.
- Use standard containers, `std::span` / `std::string_view` for borrowed data, and RAII for owned resources.
- Preserve validation, error handling and thread ownership when moving code. Avoid unrelated formatting churn.

## Game integration

- Guest hooks named in `lollipop_manifest.toml` must retain their global symbols and exact signatures.
- Never hold host runtime or registry locks across guest calls that can wait on another game thread.
- Keep generated translations separate from handwritten code; regenerate them instead of editing them.
- Settings changes must preserve native save behavior, pause state and input-release handling.

## Portable tooling

- Public scripts must work from an arbitrary checkout location, including paths containing spaces.
- Use checkout-relative defaults and explicit path parameters. Never refer to a developer's machine or sibling folders.
- Game files live in ignored `game/`, saves in `user/`, caches in `cache/`, and build/tool/generated output in `out/`.
- Fetch public dependencies from their documented, pinned source. Do not assume unpublished files are installed.
- Do not delete or overwrite existing game data or saves during setup, tests or cleanup.

## Validation and Git

- Update CMake sources, includes, tests and documentation together when moving files or changing paths.
- Build and run CTest after code changes; use an isolated save directory for in-game checks.
- Add a focused regression check for new parsing, migration, input or timing behavior.
- Keep game data, generated translations, binaries, captures, logs, prompts and session notes out of Git.
- Keep final assets and required attribution; avoid duplicate reports and implementation-history documents.
- Inspect the final diff for unintended files and machine-specific paths before committing.
