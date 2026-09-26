# FireSim on AWS

These scripts set up the machines that run Gemmini on an FPGA through FireSim (#34). Everything runs in Frankfurt (`eu-central-1`), because the team keeps its data in the EU.

There are two machines:

- **The manager** is a c5.4xlarge with no FPGA. It holds Chipyard and FireSim, builds the software, and drives the F2 over SSH.
- **The run host** is an f2.6xlarge with the FPGA. It loads the prebuilt Gemmini image and runs the binaries. We launch it ourselves (#11), because FireSim can't launch it under our IAM policy.

Both cost money while they run, the F2 most of all. Stop them when you're done (see "Daily use").

## The AWS account

The account is shared with other projects, some of them in production. Touch only resources tagged `Project=open-gpu-swiss-ai`.

The IAM policy enforces that tag:
- A launch must tag the instance, the volume and the network interface.
- It must use the default VPC and a project security group.
- We may create tagged key pairs and security groups. We may not create VPCs or internet gateways, and we may not change instance attributes.

[common.sh](common.sh) holds these settings.

On your laptop, set `AWS_PROFILE` to your project profile. The repo's `.envrc` sets the region, and `.envrc.local` (gitignored) is a good place for the profile.

## Launch a manager

```
aws/launch-manager.sh
```

The script:
1. Creates the key pair `ogsa-firesim` if it's missing, and saves the private key to `~/.ssh/ogsa-firesim.pem`. If the key pair already exists, get the `.pem` from whoever holds it.
2. Creates the security group `ogsa-firesim` if it's missing, and lets your public IP in on ports 22 and 443. On a new network, run `aws/security-group.sh` again.
3. Launches the instance with [userdata-ssh443.sh](userdata-ssh443.sh), which makes sshd listen on 443 as well as 22, because some networks block outbound port 22. It prints the `~/.ssh/config` entry to add.

## Set up the manager

Assuming the host in `~/.ssh/config` is `ogsa-firesim-manager`:

```
scp aws/manager/setup.sh aws/manager/firesim-env.sh ogsa-firesim-manager:~/
ssh ogsa-firesim-manager 'setsid nohup bash -l ~/setup.sh > ~/setup.log 2>&1 < /dev/null &'
ssh ogsa-firesim-manager 'tail -f ~/setup.log'
```

The setup is finished when the log ends with `MANAGER SETUP: DONE`. It survives a dropped SSH connection.

[manager/setup.sh](manager/setup.sh) runs FireSim's machine launch script, then installs Chipyard 1.14.0 with FireSim. It fixes four problems we hit with this AMI; each fix has a comment in the script:
- the libmamba solver isn't installed, because conda is already on the AMI;
- the glibc mismatch would force a full conda re-solve;
- Vivado is on PATH only in a login shell;
- pip breaks pyOpenSSL for the `aws` command.

Then enter your AWS credentials on the manager. Type the secret yourself, and don't paste it into a chat or a file in the repo:

```
ssh ogsa-firesim-manager
source ~/firesim-env.sh
aws configure
```

Answer with your key ID, your secret, `eu-central-1` and `json`. From then on, start each session on the manager with `source ~/firesim-env.sh`.

## Launch the F2 and point FireSim at it

```
aws/launch-run-host.sh
```

It launches an f2.6xlarge named `ogsa-firesim-f2`, and prints its private IP. Then, on the manager:

```
scp aws/manager/firesim-config.sh ogsa-firesim-manager:~/
ssh ogsa-firesim-manager
source ~/firesim-env.sh
firesim managerinit --platform f2
bash ~/firesim-config.sh <F2 private IP>
cp ~/.ssh/<the key>.pem ~/firesim.pem && chmod 600 ~/firesim.pem
firesim infrasetup
```

Notes:
- `managerinit` writes the config files, then fails with `IndexError` while looking for a "firesim" VPC. That part is only for FireSim-launched run farms, so ignore it.
- `firesim-config.sh` lists the F2 as an externally provisioned host and selects the public Gemmini image `firesim_gemmini_rocket_singlecore_no_nic` (`agfi-0f567000cb21cb06d`).
- FireSim reaches the F2 with `~/firesim.pem`, so copy the key there. The `ogsa-firesim` security group lets its members reach each other on port 22.
- `infrasetup` builds the host driver on the manager, then installs the FPGA tools on the F2 and flashes the image.

## Run a program on the FPGA

On the manager, run any bare-metal ELF that runs on Spike:

```
scp aws/manager/fpga-run.sh ogsa-firesim-manager:~/
bash ~/fpga-run.sh path/to/program-baremetal
```

The script wraps the ELF as a FireSim workload, runs `firesim infrasetup` (which copies the workload to the F2), then `firesim runworkload`, and prints the console output. A run takes about 1–1.5 minutes. `llm/fpga/f2/FIRESIM.md` has the stories260K run.

## Record a run

```
scp aws/manager/fpga-record.sh aws/manager/uart-follow.py ogsa-firesim-manager:~/
ssh -t ogsa-firesim-manager 'bash -l ~/fpga-record.sh ~/OpenASDx/llm/llm-demo-firesim NAME'
```

The script flashes the F2 off camera, then records `firesim runworkload` with asciinema while it follows the chip's console live. It writes `~/recordings/NAME.cast` and `NAME.gif`. It needs asciinema in `~/rec-venv` (`python3 -m venv ~/rec-venv && ~/rec-venv/bin/pip install asciinema`) and [`agg`](https://github.com/asciinema/agg/releases) in `~/agg`. `img/demo/README.md` explains the demo recording.

## Measure the FPGA's power

```
scp aws/manager/fpga-power.sh ogsa-firesim-f2:~/
ssh ogsa-firesim-f2 'setsid nohup bash ~/fpga-power.sh ~/power.csv >/dev/null 2>&1 < /dev/null &'
# ... run things on the manager ...
ssh ogsa-firesim-f2 'pkill -f fpga-power.sh'
```

The script reads the FPGA's core power (Vccint, in whole watts) from `fpga-describe-local-image -M` about once a second. `REPORT.md`, "FPGA power", has the results.

## Daily use

```
aws/status.sh                     # our instances, their state and IPs
aws/down.sh                       # stop all of them; or name one: aws/down.sh ogsa-firesim-f2
aws/up.sh                         # start them again and print fresh ~/.ssh/config entries
aws/ssh-config.sh                 # print those entries for whatever is running
```

- A stopped instance keeps its disk and its private IP, so FireSim's config stays valid. It costs only the disk.
- The public IP changes on every start, so update `~/.ssh/config` from `aws/up.sh`.
- These scripts only touch instances tagged for the project and named `ogsa-firesim-*`. Other instances in the account carry the tag too.

## For a colleague

1. Get an AWS CLI profile for the project, and set `AWS_PROFILE`.
2. Run `aws/security-group.sh` from your network. Someone with access can also run `aws/security-group.sh <your IP>` for you.
3. Get `ogsa-firesim.pem` from whoever holds it, and save it as `~/.ssh/ogsa-firesim.pem` with mode 600.
4. Run `aws/ssh-config.sh >> ~/.ssh/config`.

## Status

- Tested for real:
  - `launch-run-host.sh` (it started the current F2);
  - `security-group.sh`, `status.sh` and `ssh-config.sh`;
  - `firesim-config.sh`, on a copy of the manager's config.
- Not run yet:
  - `launch-manager.sh`, which uses the same launch function as the F2 script. The current manager was started with the equivalent commands by hand.
  - `up.sh` and `down.sh`.
  - `setup.sh`, which combines the steps that worked one at a time on the current manager. Nobody has run it end to end on a fresh instance.
