---
name: final-answers-and-considerations
description: Provides shared execution-efficiency, verification, git-diff, and final-answer rules; use for every task handled by a project agent.
---

# Final Answers and Considerations

Keep work focused, verify changes, and keep final responses concise across project agents.

## When to use

- Load with the `skill` tool for every task handled by a project agent, alongside its existing task-specific skills.
- Apply to task execution, verification, and final responses in every project area.
- Do not replace specialist skills, agent responsibilities, permissions, or task-specific instructions.

## Workflow

1. Minimize token use, repetitive analysis, and unnecessary explanations.
2. Inspect only the files and resources needed to complete and verify the requested task.
3. Avoid extra work that does not contribute to the requested objective.
4. Before responding, distinguish checks actually performed from checks not performed; report only real results.
5. Give explicit user instructions for a particular task priority over this skill's default answer format, including requests for only code, only a prompt, no explanations, or only a list. Preserve any existing agent-specific mandatory output contract.
6. After making changes, inspect the relevant `git diff` when Git access is available and permitted; use another permitted review method otherwise.
7. Do not modify, revert, overwrite, or include unrelated pre-existing changes.
8. Distinguish changes introduced by the current task from changes that already existed in the working tree.
9. If `git diff` reveals an unintended modification caused by the current task, correct it before completing the task when doing so is within the agent's permissions.
10. Never use `git diff` as authorization to modify files outside the agent's permitted scope.

## Git Diff Verification

- Use `git diff` after modifications when the project is a Git repository and the agent is permitted to run Git commands.
- Inspect only the relevant diff needed to verify the task.
- Account for newly created, untracked files separately: they do not appear in a plain `git diff`.
- Use the diff to confirm that requested changes were implemented correctly.
- Check for accidental or unrelated modifications.
- Preserve unrelated pre-existing working-tree changes.
- Do not automatically revert changes merely because they are unrelated to the current task.
- Do not assume every change shown by `git diff` was created by the current agent.
- Do not commit, stage, reset, restore, checkout, or discard changes unless explicitly requested and permitted.
- If Git is unavailable, forbidden by the agent's permissions, or the directory is not a Git repository, review changed files using permitted tools and do not claim that diff verification was performed.

## Output

- Respond in Spanish unless the user explicitly requests another language.
- Keep the final response brief and direct.
- When applicable, identify files created, modified, or deleted and briefly describe the changes.
- Use the relevant `git diff` as a source for the final summary of code changes when available.
- Summarize relevant changes from the diff; do not reproduce the complete `git diff` unless explicitly requested.
- Distinguish changes made by the current task from unrelated pre-existing working-tree changes when relevant.
- State which checks or tests were actually run and their results.
- Never claim an unperformed check, test, validation, or `git diff` inspection.
- Mention identified problems, limitations, incompatibilities, or conflicts.
- Clearly state when a problem or condition prevents full or partial completion.
- Do not reproduce entire files unless explicitly requested or indispensable.
- Omit unnecessary information, internal analysis, lengthy explanations, repeated details, and empty sections for categories with nothing to report.
