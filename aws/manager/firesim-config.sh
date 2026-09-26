#!/usr/bin/env bash
# Point FireSim at our F2 and the Gemmini image. Run on the manager after `firesim managerinit`:
#   bash ~/firesim-config.sh F2_PRIVATE_IP
# managerinit writes the config files, then fails creating a "firesim" VPC our policy forbids; that
# part is only for FireSim-launched run farms, which we don't use. Safe to run again.
set -euo pipefail
ip=${1:?usage: firesim-config.sh F2_PRIVATE_IP}
deploy=${FIRESIM_DEPLOY:-$HOME/chipyard/sims/firesim/deploy}

python3 - "$deploy" "$ip" <<'EOF'
import re, sys
deploy, ip = sys.argv[1:]

# Run farm: the F2 we launched, as an externally provisioned host.
p = f"{deploy}/config_runtime.yaml"
s = open(p).read()
start, end = s.index("run_farm:"), s.index("metasimulation:")
s = s[:start] + f"""run_farm:
  # OpenASDx: our IAM policy does not let FireSim launch instances, so we launch the F2 with
  # aws/launch-run-host.sh and list it here by private IP.
  base_recipe: run-farm-recipes/externally_provisioned.yaml
  recipe_arg_overrides:
    default_platform: EC2InstanceDeployManager
    default_simulation_dir: /home/ubuntu/FIRESIM_RUNS_DIR
    run_farm_hosts_to_use:
      - "{ip}": one_fpga_spec

""" + s[end:]
s = re.sub(r"default_hw_config: \S+", "default_hw_config: firesim_gemmini_rocket_singlecore_no_nic", s)
open(p, "w").write(s)

# Hardware: the public Gemmini image. The makefrag path is relative to this file, one level deeper
# than in sims/firesim-staging/sample_config_hwdb.yaml where the entry comes from.
p = f"{deploy}/config_hwdb.yaml"
s = open(p).read()
if "\nfiresim_gemmini_rocket_singlecore_no_nic:" not in s:
    s += """
# OpenASDx: the public Gemmini image (FireSimLeanGemminiRocketConfig, 30 MHz).
firesim_gemmini_rocket_singlecore_no_nic:
    agfi: agfi-0f567000cb21cb06d
    deploy_quintuplet_override: null
    deploy_makefrag_override: ../../../generators/firechip/chip/src/main/makefrag/firesim
    custom_runtime_config: null
"""
    open(p, "w").write(s)
EOF
grep -n -A2 "run_farm_hosts_to_use" "$deploy/config_runtime.yaml"
grep -n "default_hw_config" "$deploy/config_runtime.yaml"
