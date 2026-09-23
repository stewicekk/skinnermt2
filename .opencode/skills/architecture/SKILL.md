---
name: architecture
description: Production workflow for architecture
---

# architecture

## Workflow
1. Inspect the repository and existing implementation.
2. Read AGENTS.md and relevant brain documents.
3. Identify constraints, dependencies and compatibility requirements.
4. Prefer existing abstractions when correct.
5. Implement real functionality; no fake success or placeholders.
6. Add tests for behavior changes.
7. Validate malformed input and failure paths.
8. Run relevant checks and full build when required.
9. Update documentation and shared brain with verified discoveries.

## Quality
- Preserve source assets.
- Keep transformations explicit and versioned.
- Keep errors actionable.
- Avoid hidden global state and duplicated logic.
- Do not claim completion without evidence.

## Failure handling
Stop expansion on critical validation failure, reproduce the issue, fix root cause, run regression tests, then continue.
