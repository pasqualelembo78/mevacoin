#!/bin/bash
# script file used to restart a node if it gets stuck

docker stop mevacoin-node
docker rm -f mevacoin-node
docker run -d --network host --name mevacoin-node -v /root/.mevacoin:/root/.mevacoin ghcr.io/mevacoin/mevacoin:e16f2fa /bin/sh -c '/usr/src/mevacoin/start.sh'