# SKSE Plugin Template

Minimal reusable starter project for developing SKSE64 plugins with CommonLibSSE-NG and XMake.

The template provides a known-working baseline for:

- CommonLibSSE-NG
- C++23
- XMake builds
- SKSE plugin generation
- MO2 deployment through `XSE_TES5_MODS_PATH`
- Basic plugin logging

The included plugin is intentionally minimal and can be used as a smoke test before starting actual development.

## Requirements

- Visual Studio with C++ development tools
- XMake
- SKSE64
- Address Library for SKSE Plugins

## Clone

Clone recursively so CommonLibSSE-NG and its dependencies are included:

```powershell
git clone --recurse-submodules <repository-url>
```

If the repository was already cloned normally:

```powershell
git submodule update --init --recursive
```

## Build

From the project root:

```powershell
xmake build
```

To perform a clean configuration first:

```powershell
xmake f -c
xmake build
```

## MO2 Deployment

Set the user environment variable:

```text
XSE_TES5_MODS_PATH
```

to the MO2 `mods` directory for the Skyrim instance you are developing against.

Example:

```text
D:\Modding\Skyrim\MyInstance\mods
```

Then deploy the plugin with:

```powershell
xmake install
```

The normal development loop is:

```text
edit
→ xmake build
→ xmake install
→ launch Skyrim through SKSE
→ inspect plugin log
```

## Starting a New Plugin

When creating a project from this template, update the project identity in `xmake.lua`.

Change:

```lua
set_project("skse-plugin-template")
```

and:

```lua
target("skse-plugin-template")
    add_rules("commonlibsse-ng.plugin", {
        name = "skse-plugin-template",
        author = "hoskonen",
        description = "Reusable SKSE64 plugin template using CommonLibSSE-NG"
    })
```

to the name, author, description, and version appropriate for the new plugin.

Also replace this README with documentation for the actual plugin.

## Project Structure

```text
skse-plugin-template/
├─ lib/
│  └─ commonlibsse-ng/   # Git submodule
├─ src/
│  ├─ main.cpp
│  └─ pch.h
├─ .gitignore
├─ .gitmodules
├─ EXCEPTIONS
├─ LICENSE
├─ README.md
└─ xmake.lua
```

Generated build directories such as `.xmake/`, `.vs/`, `build/`, and `vsxmake*/` are not committed.

## Visual Studio

The project can be edited directly in Visual Studio Folder View.

A Visual Studio project can also be generated with XMake:

```powershell
xmake project -k vsxmake
```

## License

See [LICENSE](LICENSE) and [EXCEPTIONS](EXCEPTIONS).

## Credits

Based on CommonLibSSE-NG and its XMake plugin infrastructure.

- CommonLibSSE-NG: https://github.com/alandtse/CommonLibSSE-NG
- XMake: https://xmake.io/
- SKSE: https://skse.silverlock.org/