# Chipyard + Gemmini development environment.
# amd64 only: Chipyard's conda lock files exist only for linux-64. On Apple Silicon
# this runs under Rosetta (e.g. `colima start --vm-type vz --vz-rosetta`).
FROM --platform=linux/amd64 almalinux:9

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# AlmaLinux 9 (a RHEL 9 clone) has glibc 2.34, which is what Chipyard 1.14.0 pins (sysroot_linux-64=2.34).
# With a matching glibc, build-setup.sh installs Chipyard's tested conda lock file instead of re-solving it.
# System basics only; the toolchain, Java, sbt, Verilator and Python come from Chipyard's conda env.
RUN dnf install -y --allowerasing \
        gcc gcc-c++ make git curl wget which file bzip2 xz unzip tar diffutils patch findutils \
        sudo procps-ng less vim-minimal \
    && dnf clean all

# Miniforge: conda + mamba from conda-forge, as Chipyard's setup expects.
RUN curl -fsSL -o /tmp/miniforge.sh \
        https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-x86_64.sh \
    && bash /tmp/miniforge.sh -b -p /opt/conda \
    && rm /tmp/miniforge.sh

# A user with the host's uid, so files in bind-mounted folders keep the right owner.
ARG UID=501
ARG GID=20
RUN (getent group "$GID" >/dev/null || groupadd -g "$GID" dev) \
    && useradd -m -u "$UID" -g "$GID" -s /bin/bash dev \
    && echo 'dev ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/dev \
    && chown -R "$UID:$GID" /opt/conda \
    && mkdir -p /work/vol && chown -R "$UID:$GID" /work

USER dev
RUN /opt/conda/bin/conda init bash \
    && /opt/conda/bin/conda config --set auto_activate_base false
WORKDIR /work

# Under Rosetta, Java's default posix_spawn launcher fails ("Failed to exec spawn helper"), which breaks
# sbt when it starts subprocesses. vfork works. JDK_JAVA_OPTIONS adds to Chipyard's own JAVA_TOOL_OPTIONS.
ENV JDK_JAVA_OPTIONS=-Djdk.lang.Process.launchMechanism=VFORK
