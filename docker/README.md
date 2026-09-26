# Chipyard + Gemmini in Docker (Apple Silicon Macs)

This sets up Chipyard 1.14.0 (the commit in [`../CHIPYARD.hash`](../CHIPYARD.hash)) with Spike, the RISC-V toolchain, Verilator and this repo's Gemmini Spike model. FireSim and FireMarshal are skipped.

Chipyard's conda lock files exist only for linux-64, so the container is amd64. On Apple Silicon it runs under Rosetta. The image is AlmaLinux 9 because its glibc (2.34) matches Chipyard's pin, and with that match `build-setup.sh` installs the tested lock file instead of re-solving the environment. Python 3.10, Java, sbt and Verilator all come from Chipyard's conda environment.

## One-time host setup

```shell
brew install colima docker docker-buildx
colima start --vm-type vz --vz-rosetta --arch aarch64 --cpu 12 --memory 20 --disk 200
```

Give Colima as many CPUs as you can spare. Chipyard's Scala build wants about 8 GB of memory, and the setup uses about 16 GB of disk (a 3 GB image plus a 12 GB volume).

For `docker buildx`, add this to `~/.docker/config.json`:

```json
{ "cliPluginsExtraDirs": ["/opt/homebrew/lib/docker/cli-plugins"] }
```

Check that amd64 containers run through Rosetta:

```shell
docker run --rm --platform linux/amd64 almalinux:9 uname -m
```

It should print `x86_64`.

## Build and set up

```shell
docker/dev.sh build
docker/dev.sh setup
docker/dev.sh log
```

`setup` runs in the background and writes `/work/vol/setup.log`. It ends with `Chipyard setup: DONE`. Stop following the log with Ctrl-C; the setup keeps running.

Chipyard lives in the Docker volume `chipyard`, not in this repo. `docker/dev.sh stop` removes the container and keeps the volume. If a step fails, fix the cause and resume from that step by skipping the ones before it. For example, to resume at step 5:

```shell
docker/dev.sh setup -s 1 -s 2 -s 3
```

## Check

```shell
docker/dev.sh check
```

This runs a hello world on `spike pk`, then builds Gemmini's bare-metal tests and runs them on `spike --extension=gemmini`. Expected result: `passed 54, failed 0`. Spike is a functional simulator: it checks results, and its cycle counts do not reflect Gemmini's timing.

## Work in the container

```shell
docker/dev.sh shell
cd /work/vol/chipyard
source env.sh
```

This repo is mounted at `/work/OpenASDx`, so edits on the Mac show up in the container. After changing `software/libgemmini`, reinstall Spike's Gemmini model:

```shell
make -C /work/OpenASDx/software/libgemmini install
```

## Known issues

- Under Rosetta, Java's default process launcher fails with `Failed to exec spawn helper`, which breaks sbt. The image sets `JDK_JAVA_OPTIONS=-Djdk.lang.Process.launchMechanism=VFORK` to work around this.
- Chipyard 1.14.0's glibc check reads a trailing comment in `conda-reqs/chipyard-base.yaml`, so it never matches and re-solves the conda environment. `setup-chipyard.sh` strips the comment before running `build-setup.sh`.
