# RME Redux Commit

Commit changes to the RME Redux git repository.

## CRITICAL RULES

1. **Respond in Portuguese (pt-BR)**
2. **NEVER force push**
3. **NEVER push without explicit user confirmation**
4. **Check for sensitive files** before committing

## Workflow

### Step 1: Identify Changed Files

Read the most recent summary from `.redux/summaries/` to identify what was changed.
Also run `git status` to see the actual uncommitted changes.

### Step 2: Present Changes

Show what will be committed:

```
## Alteracoes para Commit

### Arquivos Novos
- `source/ui/dialogs/new_dialog.h`
- `source/ui/dialogs/new_dialog.cpp`

### Arquivos Modificados
- `source/CMakeLists.txt`
- `source/ui/gui.cpp`
```

### Step 3: Ask User

Use AskUserQuestion:
- Which files to include (all or select)
- Commit message confirmation
- Whether to push

### Step 4: Generate Commit Message

Based on the summary, generate a semantic commit message:

```
[Category] Brief description of what was added/changed

- Detail 1
- Detail 2

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
```

### Commit Message Prefixes

| Prefix | Usage |
|--------|-------|
| `Add` | New functionality / feature |
| `Fix` | Bug fix |
| `Update` | Enhancement to existing feature |
| `Refactor` | Code refactoring |
| `Remove` | Removing functionality |
| `UI` | Interface-only changes |

### Step 5: Execute Git Commands

```bash
# Stage specific files
git add [files...]

# Commit with HEREDOC message
git commit -m "$(cat <<'EOF'
[commit message]

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Step 6: Report Results

```
## Commit Concluido

- **Hash**: `abc1234`
- **Branch**: `master`
- **Arquivos**: X criados, Y modificados
- **Push**: [Nao solicitado / Enviado com sucesso]
```

If the user wants to push, confirm the remote and branch before executing.

## Safety Checks

Before committing:
1. **Never commit secrets** - Check for .env, credentials, API keys
2. **Verify branch** - Warn if committing to main/master directly (though this is expected for this project)
3. **No binary files** - Warn if .exe or large binaries are staged
4. **Check for conflict markers** - Scan for `<<<<<<<` in staged files
