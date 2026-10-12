# Issue tracker: Local Markdown

> 撰寫 issue 內容前，請先閱讀 `../issue-writing-guide.md` 的格式與條件；本文件負責 issue 檔案位置與狀態標記。

Issues and specs for this repo live as markdown files in `.scratch/`.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`
- The spec is `.scratch/<feature-slug>/spec.md`
- Implementation issues are one file per ticket at `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01`
- Triage state is recorded as a `Status:` line near the top of each issue file

## Triage Labels

- `ready-for-agent` — Issue is ready to be picked up by an agent for implementation
- `in-progress` — Currently being worked on
- `needs-review` — Awaiting code review
- `blocked` — Blocked by external dependency
- `done` — Completed
