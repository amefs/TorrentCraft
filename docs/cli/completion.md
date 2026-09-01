# Shell Autocompletion

The `completion` command generates ready-to-use autocompletion scripts for Bash, Zsh, and Fish shells.

```text
torrentcraft completion bash|zsh|fish
```

## Bash

```bash
mkdir -p ~/.local/share/bash-completion/completions
torrentcraft completion bash > ~/.local/share/bash-completion/completions/torrentcraft
```

Restart your terminal, or source the generated file in your current shell:

```bash
source ~/.local/share/bash-completion/completions/torrentcraft
```

## Zsh

```bash
mkdir -p ~/.zsh/completions
torrentcraft completion zsh > ~/.zsh/completions/_torrentcraft
```

Make sure `~/.zsh/completions` is included in your `$fpath` inside `~/.zshrc`:

```bash
fpath=(~/.zsh/completions $fpath)
autoload -U compinit && compinit
```

## Fish

```fish
mkdir -p ~/.config/fish/completions
torrentcraft completion fish > ~/.config/fish/completions/torrentcraft.fish
```

Fish automatically loads completions placed in this folder.

---

The generated scripts provide instant tab-completion for all top-level subcommands and common flags.
