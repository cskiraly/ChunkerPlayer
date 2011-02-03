taskkill /F /FI "IMAGENAME eq chunker_player*" /IM * /T
taskkill /F /FI "IMAGENAME eq winestreamer-ml-monl-*" /IM * /T
chunker_player.exe -q 50 -c TV1_NAPA_BBC_ALTO -p 6109
