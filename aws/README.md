# FireSim on AWS

These scripts set up the machines that run Gemmini on an FPGA through FireSim (#34). Everything runs in Frankfurt (`eu-central-1`), because the team keeps its data in the EU.

There are two machines:

- **The manager** is a c5.4xlarge with no FPGA. It holds Chipyard and FireSim, builds the software, and drives the F2 over SSH.
- **The run host** is an f2.6xlarge with the FPGA. It loads the prebuilt Gemmini image and runs the binaries. We launch it ourselves (#11), because FireSim can't launch it under our IAM policy.

Both cost money while they run. Stop them when you're done:

```
aws ec2 stop-instances --instance-ids <id>
```

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

## Status

We set up the current manager step by step, with these fixes applied one at a time. `setup.sh` puts the same steps into a single script, but nobody has run it end to end on a fresh instance yet. The launch and user-data scripts match the commands that started the current manager.
