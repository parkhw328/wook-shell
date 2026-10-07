# Repository Guidelines

## Project Structure & Module Organization

`wook-shell` is at its initial scaffold: the only existing project file is the root `README.md`, which contains the project name. No source, test, or asset directories exist yet. Keep setup instructions in `README.md`. As implementation is added, introduce clearly named directories such as `src/` and `tests/` when needed, and document their responsibilities here.

## Build, Test, and Development Commands

There is currently no package manifest, build system, or local run command. Useful repository checks are:

- `git ls-files`: list tracked project files.
- `git diff`: review unstaged changes before staging.
- `git diff --cached`: review the staged patch before committing.
- `git diff --check`: detect whitespace errors in unstaged changes.

When tooling is introduced, document its prerequisites and exact build, run, and test commands in `README.md`. Do not assume a runtime or package manager from the repository name.

## Coding Style & Naming Conventions

No language-specific style, formatter, or linter is configured. For documentation, use UTF-8, descriptive Markdown headings, fenced code blocks for multiline examples, and consistent list indentation. Prefer lowercase, hyphen-separated names for new documentation files; retain conventional names such as `README.md` and `AGENTS.md`. Establish indentation and formatting rules alongside the first source files.

## Testing Guidelines

No test framework, test naming convention, or coverage threshold exists yet. For documentation changes, review Markdown rendering and verify referenced paths and commands. When adding executable behavior, include appropriate tests, choose a consistent test naming pattern, and document how to run the suite.

## Commit & Pull Request Guidelines

Git history currently contains only `Initial commit`, so no established commit convention can be inferred. Use concise, imperative subjects such as `Document local setup`, and keep each commit focused.

Pull requests should explain the change, its purpose, and validation performed. Link related issues when applicable and include screenshots for visible UI changes. Explicitly note when automated tests are unavailable.
