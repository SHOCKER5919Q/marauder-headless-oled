#!/usr/bin/env bash
# Install the global `marauder` launcher into ~/.local/bin so you can run the
# console from anywhere. Re-run safely; it overwrites.
set -euo pipefail
PROJ="/home/shocker/Marauder/marauder-console"
BIN="$HOME/.local/bin"
mkdir -p "$BIN"
cat > "$BIN/marauder" <<INNER
#!/usr/bin/env bash
set -euo pipefail
exec "$PROJ/.venv/bin/python" "$PROJ/marauder_console.py" "\$@"
INNER
chmod +x "$BIN/marauder"
echo "installed $BIN/marauder"
case ":$PATH:" in *":$BIN:"*) echo "~/.local/bin is on PATH — just type: marauder";;
  *) echo 'Add to ~/.bashrc:  export PATH="$HOME/.local/bin:$PATH"';; esac
