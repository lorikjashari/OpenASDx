#!/usr/bin/env bash
# Run on a fresh manager, in a login shell:  bash -l ~/setup.sh > ~/setup.log 2>&1
# FireSim's machine launch script, then Chipyard 1.14.0 with FireSim (no FireMarshal, no ctags).
# Ends with "MANAGER SETUP: DONE". Step 1 of build-setup.sh refuses to run twice: to start over,
# delete ~/chipyard/.conda-env and ~/chipyard/.conda-lock-env first.
set -euxo pipefail
CHIPYARD_HASH=0acc1e1de2d3284bcd4d876956932a013ffe1949

# FireSim's step needs Vivado on PATH (AWS's FPGA SDK checks for it). /etc/profile.d/aws-f2.sh
# puts it there, but only in a login shell.
which vivado

# The AMI already has conda, so FireSim's launch script skips installing the libmamba solver, and
# the classic solver takes very long on the firesim environment. Install it first.
sudo /opt/conda/bin/conda install -y -n base conda-libmamba-solver
sudo /opt/conda/bin/conda config --system --set solver libmamba

[[ -d ~/chipyard/.git ]] || git clone https://github.com/ucb-bar/chipyard.git ~/chipyard
cd ~/chipyard
git checkout "$CHIPYARD_HASH"
git submodule update --init sims/firesim
sudo bash sims/firesim/scripts/machine-launch-script.sh
source /opt/conda/etc/profile.d/conda.sh

# The AMI has glibc 2.39 and Chipyard pins 2.34. On a mismatch build-setup.sh re-solves every
# package instead of using its tested lock file. Binaries built for an older glibc run on a newer
# one, so skip that. (build-setup.sh is a symlink to scripts/build-setup.sh.)
sed -i 's/^    if \[ "\$SYS_GLIBC" != "\$DEFAULT_GLIBC" \]; then$/    if false; then  # OpenASDx: use the tested lock file on newer glibc/' \
  scripts/build-setup.sh
grep -n "OpenASDx: use the tested lock file" scripts/build-setup.sh
./build-setup.sh --skip-marshal --skip-ctags

# pip puts cryptography 50 over conda's pyOpenSSL 23, and then `aws` in this env fails on import
# (module 'lib' has no attribute 'GEN_EMAIL'). A newer pyOpenSSL fixes it; boto3 is not affected.
set +u
source env.sh
set -u
pip install -U pyopenssl
aws --version

echo "MANAGER SETUP: DONE"
