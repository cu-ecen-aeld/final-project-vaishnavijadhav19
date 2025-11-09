#!/bin/bash



echo "Building the module..."
make

echo "Inserting the module..."
sudo insmod main.ko

echo "Checking if it was loaded..."
lsmod | grep main

echo "Module loaded sucessfully!"

