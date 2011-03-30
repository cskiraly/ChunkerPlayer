#!/bin/bash
killall chunker_player
pkill winestreamer-ml
./chunker_player -q 50 -c TV1_NAPA_BBC_ALTO -p 6109 2>/dev/null
