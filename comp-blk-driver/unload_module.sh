#!/bin/bash



echo "Removing the module..."
sudo rmmod main

echo "Checking if it was removed..."
lsmod | grep main

echo "Showing last 5 kernel messages..."
sudo dmesg | tail -n 5

echo "Module unloaded successfully!"

