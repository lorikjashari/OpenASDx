# source ~/firesim-env.sh: conda, Chipyard and the FireSim manager in the current shell.
source /opt/conda/etc/profile.d/conda.sh
cd ~/chipyard
set +u
source env.sh
cd sims/firesim
source sourceme-manager.sh --skip-ssh-setup
export AWS_DEFAULT_REGION=eu-central-1
export PYTHONWARNINGS="ignore::DeprecationWarning"
