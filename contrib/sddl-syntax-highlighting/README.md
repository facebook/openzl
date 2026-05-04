# SDDL Syntax Highlighting (VS Code)

A minimal VS Code extension providing TextMate syntax highlighting for OpenZL
SDDL2 (`.sddl`) files.

## Install (development)

Symlink this directory into your user extensions folder:

```bash
ln -s "$(pwd)" ~/.vscode/extensions/openzl.sddl-syntax-0.1.0
```

> **Meta internal users:** Use
> `~/.vscode-fb-stable/extensions/` instead

Reload VS Code (`Cmd/Ctrl+Shift+P` → "Developer: Reload Window"). Open any
`.sddl` file to verify — the status bar should show "SDDL".

## Maintenance

The token set must stay in sync with the SDDL2 tokenizer. The single source of
truth is `tools/sddl2/compiler/Syntax.cpp` (the `strs_to_syms` table) plus the
lexer rules in `tools/sddl2/compiler/tokenizer/Tokenizer.cpp`. When new
keywords or types are added there, add matching patterns to
`syntaxes/sddl.tmLanguage.json`.
