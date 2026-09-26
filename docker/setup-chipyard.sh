#!/usr/bin/env bash
# Runs inside the openasdx-dev container. Clones Chipyard at the commit in CHIPYARD.hash
# into /work/vol/chipyard (the Docker volume) and runs its setup for Spike + Verilator work.
# FireSim and FireMarshal are skipped. Safe to rerun: an existing checkout is reused.
set -euo pipefail

REPO=/work/OpenASDx
CY=/work/vol/chipyard
HASH="$(tr -d '[:space:]' < "$REPO/CHIPYARD.hash")"

source /opt/conda/etc/profile.d/conda.sh

if [[ ! -d "$CY/.git" ]]; then
    git clone https://github.com/ucb-bar/chipyard.git "$CY"
fi
cd "$CY"
git fetch --quiet origin
git checkout --quiet "$HASH"
echo "Chipyard at $(git rev-parse --short HEAD) (CHIPYARD.hash $HASH)"

# build-setup.sh compares the system glibc with the sysroot pin in chipyard-base.yaml, but its awk
# also picks up the trailing comment ("2.34 # need to be close ..."), so the check never matches and
# it re-solves the whole conda env instead of using the tested lock file. Strip the comment.
sed -i 's/^\([[:space:]]*- sysroot_linux-64=[0-9.]*\)[[:space:]]*#.*/\1/' conda-reqs/chipyard-base.yaml
echo "glibc: system $(ldd --version | awk '/ldd/{print $NF}'), pinned $(grep -i 'sysroot_linux-64=' conda-reqs/chipyard-base.yaml | awk -F= '{print $2}')"

# Extra arguments go to build-setup.sh, e.g. "-s 1 -s 2 -s 3" to resume at step 5 after a failure.
# Skipping step 1 (conda) needs the environment that step created. env.sh reads unset variables.
if [[ " $* " =~ \ (--skip-conda|-s\ 1|--skip\ 1)\  ]]; then
    set +u; source env.sh; set -u
fi
./build-setup.sh riscv-tools --use-lean-conda --skip-firesim --skip-marshal --skip-ctags "$@"

# Spike's Gemmini model: use this repo's libgemmini instead of the copy build-setup.sh installed.
set +u; source env.sh; set -u
make -C "$REPO/software/libgemmini" install
echo "Chipyard setup: DONE"
