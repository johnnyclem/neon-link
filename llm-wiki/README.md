# neon-link LLM Wiki

An LLM-maintained knowledge base for the **neon-link** firmware (ESP32
Ableton Link peer + clock/CV/MIDI hardware, multiple board targets). It is
written and maintained by the LLM agent, read by humans. It sits *above* the
code and the human docs in `docs/` — it accumulates the hard-won,
cross-cutting knowledge that is painful to re-derive: board capabilities,
subsystem mechanics, and the debugging gotchas that cost hours.

This is not RAG. Each work session or investigation is *ingested*: the agent
extracts the durable knowledge, updates the relevant entity/concept pages,
and logs what happened — so the next session starts from the compiled
understanding, not from scratch.

## Layers
- **Raw sources** — the code, `docs/`, git history, and the conversation
  transcripts of work sessions. Immutable; the wiki reads from them.
- **The wiki** — this directory. LLM-owned markdown.
- **The schema** — this file. How the wiki is structured and maintained.

## Structure
```
llm-wiki/
  README.md            this file (schema + conventions)
  index.md             catalog of every page, by category
  log.md               append-only chronological record
  boards/              one page per hardware target (capabilities, pins, gotchas)
  concepts/            cross-cutting subsystems (midi clock path, wifi, config store…)
  lessons/             debugging gotchas & dead-ends, so we don't repeat them
  sessions/            one page per work session (YYYY-MM-DD-slug.md)
```
Add categories as needed (e.g. `web/`, `audio/`). Keep pages focused; split
when one grows past what you'd read in a sitting.

## Conventions
- **Links** are relative markdown so they render on GitHub, e.g.
  `[MaTouch](boards/matouch.md)`. Link liberally between pages.
- **Anchor claims to code**: cite `path:line`, a commit hash, or a doc.
  Prefer durable anchors (file/function names) over line numbers where the
  code may move.
- **Prune, don't append forever.** When a session supersedes an old claim,
  edit the concept/board page in place and note the change; the session page
  keeps the narrative.
- **Dates are absolute** (`2026-08-27`), never "today".
- Session pages are the narrative record; concept/board/lessons pages are the
  distilled, always-current truth. A single ingest usually touches several.

## Operations
- **Ingest a session**: write `sessions/<date>-<slug>.md`; fold durable facts
  into `boards/` `concepts/` `lessons/`; update `index.md`; append to `log.md`.
- **Query**: read `index.md` first, then drill into pages; cite sources. File
  a genuinely reusable answer back as a concept/lessons page.
- **Lint**: look for contradictions, stale claims newer sessions superseded,
  orphan pages, concepts mentioned but lacking a page, missing cross-links.

Maintained by the LLM agent. Humans: curate the work, ask the questions.
