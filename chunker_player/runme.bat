taskkill /F /FI "IMAGENAME eq chunker_player*" /IM * /T
taskkill /F /FI "IMAGENAME eq winestreamer-ml-monl-*" /IM * /T
chunker_player.exe -q 50 -c NAPA-WINE_workshop -p 6109
