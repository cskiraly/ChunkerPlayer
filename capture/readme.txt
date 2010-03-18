Il programma salva i fotogrammi nella cartella frames e l'audio in formato PCM nel file out.wav.

Per compilare: make.

Dipendenze:
- alsa development libraries. Il pacchetto e' chiamato con un nome differente a seconda della distribuzione utilizzata. Tipicamente e' qualcosa tipo: "libasound-dev" o "alsa-lib-devel";
- L'header file di v4l2 (videodev2.h). Questo dovrebbe essere contenuto nei kernel headers del sistema.

Per visualizzare l'help, utilizzare il parametro "-h".

E' possibile specificare il formato video di acquisizione desiderato. Tuttavia, in base al driver della webcam utilizzato, alcuni formati potrebbero non essere disponibili.
Se appare un errore simile a "VIDIOC_S_FMT error 22, Invalid argument", riprovare cambiando il formato desiderato tramite i parametri da linea di comando.
Ho fatto in modo che possa essere scelto uno tra i formati piu' comuni.

Se dovessero apparire errori tipo "Input/output error", riprovare specificando un altro audio device tramite i parametri da linea di comando.
