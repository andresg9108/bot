---
name: responder-ia
description: Records answers and read-only results in the project-root IA.txt; use ONLY when the user requests that complete result there.
---

# Answer Questions and Tasks in IA.txt

Answer a question or complete a read-only task, append the complete result to `IA.txt`, and leave every other project file unchanged.

## When to use

- The user explicitly asks for a question answer or task result to be written to `IA.txt`.
- The request can be completed with read-only tools without changing other files or external systems.
- Do not use this skill when the task requires edits, shell commands, deployments, or other state changes.

## Workflow

1. Confirm that the request can be completed without modifying any file except `IA.txt`.
2. Solve the question or complete the read-only task using available project context.
3. If `IA.txt` does not exist, create it at the project root with a top-level `# IA` heading.
4. If it exists, read enough of the end to preserve its current content and formatting.
5. Append a new entry without rewriting or reordering previous entries:

```markdown
## YYYY-MM-DD — <question or task>

<complete answer or result>
```

6. Use the runtime date in `YYYY-MM-DD` format and preserve the user's request as a concise heading.
7. If the task requires another file or system change, append the limitation and the clarification needed instead of attempting that change.
8. Do not run shell commands or edit any file other than `IA.txt`.
9. Confirm that the entry was appended and no other file changed.

## Output

After appending the entry, reply with exactly this single line: `Result added to IA.txt.`
