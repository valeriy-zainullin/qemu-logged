#!/bin/bash

echo "Hello from HOST!" | nc -u -c 127.0.0.1 5555
