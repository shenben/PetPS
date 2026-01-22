#!/bin/bash
cd $(dirname $(readlink -f $0))

addr=$(head -1 ../memcached.conf)
port=$(awk 'NR==2{print}' ../memcached.conf)

echo "Memcached config: $addr:$port"

# Memcached is already running on both nodes, just init the counters
echo "Using existing memcached at $addr:$port"

# init
echo -e "set serverNum 0 0 1\r\n0\r\nquit\r" | nc ${addr} ${port}
echo -e "set clientNum 0 0 1\r\n0\r\nquit\r" | nc ${addr} ${port}

sleep 1