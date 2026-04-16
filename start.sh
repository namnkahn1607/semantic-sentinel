#!/bin/bash

# 1. Clear 'garbage' socket from previous run
rm -f /tmp/sentinel.sock

# 2. Run Vector Engine
echo "Starting C++ Semantic Engine on Cores 1, 2, and 3..."
taskset -c 1,2,3 ./sentinel_engine &

# 3. Wait 1s for Vector Engine to setup UDS socket
sleep 1

# 4. Run HTTP Gateway
echo "Starting Go Gateway on Core 0..."
taskset -c 0 ./gateway 
