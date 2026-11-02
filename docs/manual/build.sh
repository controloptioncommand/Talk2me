#!/usr/bin/env bash
# Пересобирает docs/Talk2Me-manual.pdf: свежие скриншоты из теста → нарезка → печать HTML в PDF.
# Нужны: Python 3 с Pillow, Node.js с Playwright. На Linux тест запускается через xvfb-run.
set -euo pipefail
cd "$(dirname "$0")/../.."

TEST=./build/T2MTest_artefacts/Release/T2MTest
SHOTS=$(mktemp -d)
trap 'rm -rf "$SHOTS"' EXIT

[ -d build ] || cmake -B build -DT2M_BUILD_TESTS=ON
cmake --build build --target T2MTest
RUN=()
if [[ "$(uname)" == "Linux" ]]; then RUN=(xvfb-run -a); fi
"${RUN[@]}" "$TEST" "$SHOTS" | tail -1

python3 docs/manual/crop.py "$SHOTS" docs/manual/img
NODE_PATH="${NODE_PATH:-$(npm root -g)}" node docs/manual/print.js docs/manual/Talk2Me-manual.html docs/Talk2Me-manual.pdf
echo "docs/Talk2Me-manual.pdf: $(python3 -c 'import os; print(os.path.getsize("docs/Talk2Me-manual.pdf") // 1024)') KB"
