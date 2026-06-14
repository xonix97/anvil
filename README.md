# Anvil ⚒️

A **BYOK agentic coding assistant** for your terminal — like Codex / Claude Code, written in C++. Plug in your own API key, give it a task, and it reads files, writes code, and runs commands through an agent loop. Defaults to any **OpenAI-compatible** API; **Anthropic (Claude)** supported via config.

> © 2026 Abhyudaya Mishra. All rights reserved. Proprietary — see [LICENSE](./LICENSE).

![Anvil UI preview](assets/preview.svg)

> The image above is a UI preview (a faithful render of the interface), not a photo of a run.

## Download

Grab the latest Windows build from **[Releases → latest](../../releases/latest)** (`anvil.exe`). It's compiled by GitHub Actions on a Windows runner — see [`.github/workflows/build.yml`](.github/workflows/build.yml).

## What it does

- **Agent loop** — sends your task to the model, runs the tools it asks for, feeds results back, repeats until done.
- **Tools:** `read_file`, `write_file`, `list_dir`, `run_command`.
- **Safe by default** — writing files and running commands ask for `[y/N]` confirmation first.
- **Colored terminal UI** with a clean prompt.
- **No external HTTP deps** — uses Windows' built-in WinHTTP. JSON via nlohmann/json.

## Configure (Bring Your Own Key)

Set environment variables:

```bat
set ANVIL_API_KEY=sk-your-key
set ANVIL_MODEL=gpt-4o-mini
:: optional
set ANVIL_PROVIDER=openai            & rem  openai (default) | anthropic
set ANVIL_BASE_URL=https://api.openai.com
```

…or drop a config file at `%USERPROFILE%\.anvil\config.json`:

```json
{
  "provider": "openai",
  "apiKey": "sk-your-key",
  "model": "gpt-4o-mini",
  "baseUrl": "https://api.openai.com"
}
```

For Claude:
```json
{ "provider": "anthropic", "apiKey": "sk-ant-...", "model": "claude-3-5-sonnet-latest" }
```

It also falls back to `OPENAI_API_KEY` / `ANTHROPIC_API_KEY` if `ANVIL_API_KEY` isn't set.

## Use

```text
anvil› refactor utils.cpp to remove the duplicated parsing code
```

Anvil will inspect the project, propose edits, and ask before writing or running anything.

## Build it yourself

```bash
cmake -B build
cmake --build build --config Release
```

Needs CMake ≥ 3.16 and a C++17 compiler. On Windows it links WinHTTP automatically.

## License
Proprietary — All Rights Reserved. See [LICENSE](./LICENSE).
