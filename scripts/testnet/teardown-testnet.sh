#!/bin/sh
# This file is intended to do a teardown of the containers running on your computer if you don't
# want it to be running anymore. But still want to be able to fast start the containers again.

# removing network and stopping all containers
docker-compose down

# removing containers
docker rm mevacoin-node1
docker rm mevacoin-node2
docker rm mevacoin-node3
docker rm mevacoin-miner1