#!/bin/bash
killall chunker_player
pkill winestreamer-ml-monl-
./chunker_player -q 50 -c NAPA-WINE_workshop -p 6109 2>/dev/null
