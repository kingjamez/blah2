#!/bin/bash
# Stop blah2 services

echo "Stopping blah2..."
systemctl stop blah2 2>/dev/null
systemctl stop blah2-api 2>/dev/null

# Catch any manually started instances too
killall blah2 2>/dev/null
kill $(lsof -ti :3000) 2>/dev/null

echo "blah2 stopped."
