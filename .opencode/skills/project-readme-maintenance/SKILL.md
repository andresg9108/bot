---
name: project-readme-maintenance
description: Maintains agbot README.md files; use ONLY when the user asks to create, update, review, or audit this ESP-IDF project's README documentation.
---

# Project README Maintenance

Maintain accurate, useful README files by grounding every change in verifiable project sources and preserving the target document's established conventions.

## When to use

- The user requests a new README or changes to an existing `README.md` file.
- ESP-IDF setup, build commands, component layout, firmware behavior, or usage documentation must be created or corrected.
- Do not use this skill for application code, OpenCode configuration, or documentation other than `README.md`.

## Workflow

1. Identify the exact README target. If several files could match and the user did not select one, request clarification.
2. Read the complete target file, `AGENTS.md`, root and component CMake files, and the firmware sources needed to verify the affected claims. Load `esp-idf-firmware` when describing ESP-IDF setup or behavior.
3. Preserve the target's language, structure, tone, formatting, valid examples, and unrelated content. Repository documentation that is already in Spanish should remain in Spanish unless the user requests a change.
4. Document only verified commands, paths, dependencies, environment variables, component registration, and firmware behavior. `idf.py build` needs an initialized ESP-IDF environment and an intended chip target; do not infer a target, claim that `agBLE/` is registered, or present a recommendation as an implemented fact.
5. Update the smallest relevant section, avoid duplication, and do not create or edit any non-README file.
6. Re-read the result, check Markdown correctness and internal consistency, and compare every changed claim with its source again.
7. If command execution is available and permitted, validate documented commands where safe; otherwise state that only source-based verification was possible. Do not create `sdkconfig` or run `idf.py set-target` merely to verify documentation.

## Output

Report the README files changed, the verified content improvements, and the checks performed. Keep user-facing README content in the target document's established language, and state when runtime validation was not performed.
