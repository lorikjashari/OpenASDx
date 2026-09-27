#!/usr/bin/env bash
# Pack llm/ (the tracked files, as they are now) for colab.ipynb, which uploads it: the repo is private.
#   llm/bench/colab-bundle.sh      writes openasdx-llm.tar.gz in the current directory
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
(cd "$repo" && git ls-files llm | grep -v '^llm/bench/results/' | tar czf - -T -) > openasdx-llm.tar.gz
ls -lh openasdx-llm.tar.gz
