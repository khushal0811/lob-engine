# Agent index — lob-engine

Read this file at the start of every session before doing anything else.
Then read only the sections you need from the files listed below.

## Files in this folder

### AGENT_INDEX.md (this file)
- Read first, every session, no exceptions
- Tells you what to read and where to find it

### scope.md
- Read when: you need to know what is in scope vs out of scope
- Contains: order types, matching rules, module list, validation rules, excluded features

### architecture.md
- Read when: you need to understand how modules connect, data structures, or design decisions
- Contains: full data flow, directory structure, every module's responsibility, key design decisions and rationale

### build-plan.md
- Read when: you want a quick overview of the 6 phases and what each delivers
- Contains: one-paragraph summary per phase, summary table

### implementation-plan.md
- Read when: you are about to implement any part of any phase
- Contains: exact file paths, full code, commit messages, checklists
- How to use: find the current phase and part number, read only that section

## Current project state
- Current phase: 6 (COMPLETE)
- Current part: 6.5 (all parts done)
- Last completed part: 6.5 (sample replay data)
- Last commit: data: sample replay datasets for small_market, stop, and iceberg scenarios

## Rules for every session
1. Read this file first
2. Check "Current project state" to know where you are
3. Read only the relevant section of implementation-plan.md for the current part
4. Implement exactly as specified — no deviations
5. Use filesystem MCP to write files to: `/Users/khushalarora/Documents/Career/Project_1/lob-engine/`
6. After completing each part, update "Current project state" in this file.
7. Never read the full implementation-plan.md in one go — read only the current part

After each part is done, just tell the agent: "Update AGENT_INDEX.md — current part is now 1.2, last completed part is 1.1, last commit is build: cmake foundation with googletest, directory structure."
That single file becomes the agent's persistent memory across every session. Massive token savings.