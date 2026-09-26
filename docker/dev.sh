#!/usr/bin/env bash
# Host-side helper for the Chipyard/Gemmini container.
#   docker/dev.sh build     build the openasdx-dev image
#   docker/dev.sh setup     clone + set up Chipyard in the background (log: docker/dev.sh log)
#                           extra arguments go to build-setup.sh, e.g. "setup -s 1 -s 2 -s 3" resumes at step 5
#   docker/dev.sh log       follow the setup log
#   docker/dev.sh check     run a hello world and Gemmini's bare-metal tests on Spike
#   docker/dev.sh shell     open a shell in the running container (starts it if needed)
#   docker/dev.sh stop      stop and remove the container (the Chipyard volume is kept)
# This repo is mounted at /work/OpenASDx; the Docker volume "chipyard" is /work/vol (Chipyard in /work/vol/chipyard, log in /work/vol/setup.log).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=openasdx-dev
NAME=openasdx
VOLUME=chipyard

start() {
    if ! docker container inspect "$NAME" >/dev/null 2>&1; then
        docker run -d --name "$NAME" --platform linux/amd64 \
            -v "$VOLUME:/work/vol" \
            -v "$REPO_DIR:/work/OpenASDx" \
            "$IMAGE" sleep infinity >/dev/null
    elif [[ "$(docker container inspect -f '{{.State.Running}}' "$NAME")" != "true" ]]; then
        docker start "$NAME" >/dev/null
    fi
}

case "${1:-shell}" in
    build)
        docker build --platform linux/amd64 --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" \
            -t "$IMAGE" "$REPO_DIR/docker"
        ;;
    setup)
        shift
        start
        docker exec -d "$NAME" bash -lc \
            'bash /work/OpenASDx/docker/setup-chipyard.sh "$@" > /work/vol/setup.log 2>&1' bash "$@"
        echo "Setup running in the background. Follow it with: docker/dev.sh log"
        ;;
    log)
        docker exec "$NAME" tail -f /work/vol/setup.log
        ;;
    check)
        start
        docker exec "$NAME" bash -l /work/OpenASDx/docker/check-spike.sh
        ;;
    shell)
        start
        docker exec -it "$NAME" bash -l
        ;;
    stop)
        docker rm -f "$NAME" >/dev/null && echo "stopped (volume $VOLUME kept)"
        ;;
    *)
        sed -n '2,10p' "$0"
        exit 2
        ;;
esac
