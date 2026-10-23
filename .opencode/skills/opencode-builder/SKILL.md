---
name: opencode-builder
description: Maintains OpenCode agents, skills, and commands; use ONLY when creating, editing, reviewing, or auditing those assets under .opencode.
---

# Build OpenCode Agents and Skills

Create and maintain OpenCode configuration assets only under `.opencode/`, using canonical project structure and English-only content.

## When to use

- The user asks to create, edit, review, or audit a project agent, skill, or command.
- The target is an agent Markdown file, a skill `SKILL.md`, a command file, or `.opencode/opencode.json`.
- Do not use this skill for application code or project documentation outside `.opencode/`.

## Workflow

1. Inventory the relevant files under `.opencode/agents/`, `.opencode/skills/`, and `.opencode/command/` (if present) before editing.
2. Read every target file and the project sources needed to verify its claims; load `esp-idf-firmware` when writing or reviewing firmware-specific guidance.
3. Write every agent and skill Markdown file entirely in English, including frontmatter.
4. Use only these allowed locations:

| Asset | Path |
| --- | --- |
| Agent | `.opencode/agents/<name>.md` |
| Skill | `.opencode/skills/<name>/SKILL.md` |
| Command | `.opencode/command/<name>.md` |
| Project config | `.opencode/opencode.json` |

5. Never create or edit files outside `.opencode/`.
6. For this project's OpenCode 1.x configuration, use singular `permission` and the `bash` permission key; do not invent `permissions` or a `shell` permission key.
7. Follow the canonical structures below, adapting mode and permissions to the role; do not put prompt text in frontmatter.
8. Keep skill names lowercase and hyphen-separated, no longer than 64 characters, and identical to their folder names.
9. Give every skill a concise, third-person `description` that states what it does and when to use it; gate specialized skills with `Use ONLY when...`.
10. Keep agent descriptions in one sentence that states what the agent does and when to use it.
11. Order permission patterns from broad deny rules to narrow allow rules because the last matching rule wins.
12. Verify cross-references use exact skill IDs and that every agent points to its intended skill. Remove guidance inherited from unrelated projects rather than inventing features for this one.
13. Re-read changed files and confirm that no file outside `.opencode/` was changed by this work. For diff review, follow `final-answers-and-considerations` when Git access is permitted.

### Canonical agent structure

```markdown
---
description: What the agent does and when to use it, in one third-person sentence.
mode: primary
permission:
  edit:
    "*": deny
    ".opencode/**": allow
  bash: deny
---

# <Agent Name>

You are <role> in the <project> codebase.

## Role

<What this agent does and its responsibilities.>

## When to use

<The situations in which this agent should be invoked.>

## Rules

- <Permission and scope rules.>
- <Required skills and project conventions.>
- <Safety, accuracy, and output requirements.>

## Workflow

1. <Preparation.>
2. <Implementation.>
3. <Verification.>

## Output

<Deliverable format and required verification results.>
```

- Allowed agent frontmatter fields are `name`, `model`, `variant`, `description`, `mode`, `hidden`, `color`, `steps`, `options`, `permission`, `disable`, `temperature`, and `top_p`.
- Valid modes are `primary`, `subagent`, and `all`.
- The Markdown body is the agent prompt; never add a `prompt` frontmatter field.

### Canonical skill structure

```markdown
---
name: <folder-name>
description: What the skill does and when to use it, in one third-person sentence.
---

# <Skill Name>

<Short summary of the skill's purpose.>

## When to use

- <Trigger condition.>
- <Task type covered.>
- <Adjacent task that must not load the skill.>

## Workflow

1. <Preparation and prerequisites.>
2. <Procedure.>
3. <Verification.>

## Output

<Required result format, files, naming, and language rules.>
```

## Output

Report changed files under `.opencode/` and structural improvements using `final-answers-and-considerations`. Tell the user to quit and restart OpenCode because configuration is loaded at startup.
