#include <stdio.h>
#include <unistd.h>
#include <SDL.h>
#include <SDL_mutex.h>
#include "player_defines.h"
#include <confuse.h>
#include "http_default_urls.h"
#include "player_defines.h"
#include "chunker_player.h"
#include "player_gui.h"

int main(int argc, char *argv[])
{
	// some initializations
	SilentMode = 0;
	queue_filling_threshold = 0;
	SaveYUV = 0;
	quit = 0;
	QueueFillingMode=1;
	P2PProcessID = -1;
	NChannels = 0;
	SelectedChannel = -1;
	char firstChannelName[255];
	int firstChannelIndex;
	
	memset((void*)Channels, 0, (MAX_CHANNELS_NUM*sizeof(SChannel)));

	HttpPort = -1;
	struct MHD_Daemon *daemon = NULL;
	SDL_Event event;
	OverlayMutex = SDL_CreateMutex();

	FILE *fp;
		
	if(argc<6) {
		printf("chunker_player queue_thresh httpd_port silentMode LossTracesFilenameSuffix ChannelName <YUVFilename>\n");
		exit(1);
	}
	sscanf(argv[1],"%d",&queue_filling_threshold);
	sscanf(argv[2],"%d",&HttpPort);
	sscanf(argv[3],"%d",&SilentMode);
	sscanf(argv[4],"%s",LossTracesFilename);
	sscanf(argv[5],"%s",firstChannelName);
	
	if(argc==7)
	{
		sscanf(argv[6],"%s",YUVFileName);
		printf("YUVFile: %s\n",YUVFileName);
		fp=fopen(YUVFileName, "wb");
		if(fp)
		{
			SaveYUV=1;
			fclose(fp);
		}
		else
			printf("ERROR: Unable to create YUVFile\n");
	}
	
	char filename[255];
	sprintf(filename, "audio_%s", LossTracesFilename);
	fp=fopen(filename, "wb");
	if(fp)
	{
		fclose(fp);
		sprintf(filename, "video_%s", LossTracesFilename);
		fp=fopen(filename, "wb");
		if(fp)
			fclose(fp);
		else
		{
			printf("ERROR: Unable to create loss trace files\n");
			exit(1);
		}
	}
	else
	{
		printf("ERROR: Unable to create loss trace files\n");
		exit(1);
	}

	//this thread fetches chunks from the network by listening to the following path, port
	daemon = (struct MHD_Daemon*)initChunkPuller(UL_DEFAULT_EXTERNALPLAYER_PATH, HttpPort);
	if(daemon == NULL)
	{
		printf("CANNOT START MICROHTTPD SERVICE, EXITING...\n");
//		KILLALL("offerstreamer");
		exit(2);
	}

	if(!SilentMode)
	{
		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
			fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
			return -1;
		}
	}
	else
	{
		if(SDL_Init(SDL_INIT_TIMER)) {
			fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
			return -1;
		}
	}
	
	if(ParseConf())
	{
		printf("ERROR: Cannot parse configuration file, exit...\n");
		exit(1);
	}
	
	firstChannelIndex = -1;
	int it;
	for(it = 0; it < NChannels; it++)
	{
		if(!strcmp(Channels[it].Title, firstChannelName))
		{
			firstChannelIndex = it;
			break;
		}
	}
	
	if(firstChannelIndex < 0)
	{
		printf("Cannot find the specified channel (%s) into the configuration file (channels.conf), exiting\n");
		exit(0);
	}
	
	if(ChunkerPlayerGUI_Init())
	{
		printf("ERROR: Cannot init player gui, exit...\n");
		exit(1);
	}
	
	SelectedChannel = firstChannelIndex;

	SwitchChannel(&(Channels[SelectedChannel]));

	// Wait for user input
	while(!quit) {
		if(QueueFillingMode) {
			SDL_WM_SetCaption("Filling buffer...", NULL);

			if(ChunkerPlayerCore_AudioEnded())
				ChunkerPlayerCore_ResetAVQueues();

#ifdef DEBUG_QUEUE
			//printf("QUEUE: MAIN audio:%d video:%d audiolastframe:%d videolastframe:%d\n", audioq.nb_packets, videoq.nb_packets, audioq.last_frame_extracted, videoq.last_frame_extracted);
#endif
		}
		else
			SDL_WM_SetCaption("NAPA-Wine Player", NULL);

		//listen for key and mouse
		while(SDL_PollEvent(&event)) {
			switch(event.type) {
				case SDL_QUIT:
					quit=1;
				break;
				case SDL_VIDEORESIZE:
					if(SilentMode)
						break;
					// printf("\tSDL_VIDEORESIZE event received!! \n");
					if(!FullscreenMode)
						ChunkerPlayerGUI_HandleResize(event.resize.w, event.resize.h);
					else
						ChunkerPlayerGUI_HandleResize(FullscreenWidth, FullscreenHeight);
				break;
				case SDL_ACTIVEEVENT:
					if(SilentMode)
						break;
						
					// if the window was iconified or restored
					if(event.active.state & SDL_APPACTIVE)
					{
						//If the application is being reactivated
						if( event.active.gain != 0 )
						{
							ChunkerPlayerGUI_HandleGetFocus();
						}
					}

					//If something happened to the keyboard focus
					else if( event.active.state & SDL_APPINPUTFOCUS )
					{
						//If the application gained keyboard focus
						if( event.active.gain != 0 )
						{
							ChunkerPlayerGUI_HandleGetFocus();
						}
					}
					//If something happened to the mouse focus
					else if( event.active.state & SDL_APPMOUSEFOCUS )
					{
						//If the application gained mouse focus
						if( event.active.gain != 0 )
						{
							ChunkerPlayerGUI_HandleGetFocus();
						}
					}
					break;
				case SDL_MOUSEMOTION:
					if(SilentMode)
						break;
						
					ChunkerPlayerGUI_HandleMouseMotion(event.motion.x, event.motion.y);
				break;
				case SDL_MOUSEBUTTONUP:
					if(SilentMode)
						break;
						
					if( event.button.button != SDL_BUTTON_LEFT )
						break;

					ChunkerPlayerGUI_HandleLButton(event.motion.x, event.motion.y);
				break;
			}
			ChunkerPlayerGUI_HandleKey();
		}
		usleep(120000);
	}
	
	if(P2PProcessID > 0)
		KILL_PROCESS(P2PProcessID);

	//TERMINATE
	ChunkerPlayerCore_Stop();
	if(YUVOverlay != NULL)
		SDL_FreeYUVOverlay(YUVOverlay);
	
	ChunkerPlayerGUI_Close();
	SDL_DestroyMutex(OverlayMutex);
	SDL_Quit();
	finalizeChunkPuller(daemon);
	return 0;
}

int ParseConf()
{
	int j;
	
	// PARSING CONF FILE
	cfg_opt_t channel_opts[] =
	{
		CFG_STR("Title", "", CFGF_NONE),
		CFG_STR("LaunchString", "", CFGF_NONE),
		CFG_INT("AudioChannels", 2, CFGF_NONE),
		CFG_INT("SampleRate", 48000, CFGF_NONE),
		CFG_INT("Width", 176, CFGF_NONE),
		CFG_INT("Height", 144, CFGF_NONE),
		CFG_FLOAT("Ratio", 1.22, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t opts[] =
	{
		CFG_STR("PeerExecName", DEFAULT_CHANNEL_EXEC_NAME, CFGF_NONE),
		CFG_SEC("Channel", channel_opts, CFGF_TITLE | CFGF_MULTI),
		CFG_END()
	};
	cfg_t *cfg, *cfg_channel;
	cfg = cfg_init(opts, CFGF_NONE);
	if(cfg_parse(cfg, DEFAULT_CONF_FILENAME) == CFG_PARSE_ERROR)
	{
		printf("Error while parsing configuration file, exiting...\n");
		return 1;
	}
	
	if(cfg_parse(cfg, DEFAULT_CONF_FILENAME) == CFG_FILE_ERROR)
	{
		printf("Error trying parsing configuration file. '%s' file couldn't be opened for reading\n", DEFAULT_CONF_FILENAME);
		return 1;
	}
	
	sprintf(OfferStreamerFilename, "%s", cfg_getstr(cfg, "PeerExecName"));
	
	FILE * tmp_file;
	if(tmp_file = fopen(OfferStreamerFilename, "r"))
    {
        fclose(tmp_file);
    }
    else
	{
		printf("Could not find peer application (named '%s') into the current folder, please copy or link it into the player folder, then retry\n\n", OfferStreamerFilename);
		exit(1);
	}
	
	for(j = 0; j < cfg_size(cfg, "Channel"); j++)
	{
		cfg_channel = cfg_getnsec(cfg, "Channel", j);
		sprintf(Channels[j].Title, "%s", cfg_title(cfg_channel));
		strcpy(Channels[j].LaunchString, cfg_getstr(cfg_channel, "LaunchString"));
		Channels[j].Width = cfg_getint(cfg_channel, "Width");
		Channels[j].Height = cfg_getint(cfg_channel, "Height");
		Channels[j].AudioChannels = cfg_getint(cfg_channel, "AudioChannels");
		Channels[j].SampleRate = cfg_getint(cfg_channel, "SampleRate");
		Channels[j].Ratio = cfg_getfloat(cfg_channel, "Ratio");
		Channels[j].Index = j+1;
		NChannels++;
	}
	cfg_free(cfg);

	return 0;
}

int SwitchChannel(SChannel* channel)
{
	int i=0;
#ifdef RESTORE_SCREEN_ON_ZAPPING
	int was_fullscreen = FullscreenMode;
	int old_width = window_width, old_height = window_height;
#endif
	
	if(ChunkerPlayerCore_IsRunning())
		ChunkerPlayerCore_Stop();

	if(P2PProcessID > 0)
		KILL_PROCESS(P2PProcessID);
	
	ChunkerPlayerGUI_SetChannelTitle(channel->Title);
	ChunkerPlayerGUI_ForceResize(channel->Width, channel->Height);
	
	ChunkerPlayerCore_SetupOverlay(channel->Width, channel->Height);
	
	ChunkerPlayerCore_InitCodecs(channel->Width, channel->Height, channel->SampleRate, channel->AudioChannels);
		
	char* parameters_vector[255];
	char argv0[255], parameters_string[511];
	sprintf(argv0, "%s", OfferStreamerFilename);
	
	sprintf(parameters_string, "%s %s %s %s %s %d %s %d", argv0, channel->LaunchString, "-C", channel->Title, "-P", (HttpPort+channel->Index), "-F", HttpPort);
	
	printf("EXECUTING %s\n", parameters_string);
	
	int par_count=0;
	
	// split parameters and count them
	char* pch = strtok (parameters_string, " ");
	
	while (pch != NULL)
	{
		if(par_count > 255) break;
		// printf ("\tpch=%s\n",pch);
		parameters_vector[par_count] = (char*) malloc(sizeof(char)*(strlen(pch)+1));
		strcpy(parameters_vector[par_count], pch);
		pch = strtok (NULL, " ");
		par_count++;
	}
	parameters_vector[par_count] = NULL;

	//reset quality info
	channel->startTime = time(NULL);
	channel->instant_score = 0.0;
	channel->average_score = 0.0;
	channel->history_index = 0;
	for(i=0; i<CHANNEL_SCORE_HISTORY_SIZE; i++)
		channel->score_history[i] = -1;
	sprintf(channel->quality, "EVALUATING...");

#ifdef __LINUX__

	int d;
	int stdoutS, stderrS;
	FILE* stream;
	stream = fopen("/dev/null", "a+");
	d = fileno(stream);

	// create backup descriptors for the current stdout and stderr devices
	stdoutS = dup(STDOUT_FILENO);
	stderrS = dup(STDERR_FILENO);
	
	// redirect child output to /dev/null
	dup2(d, STDOUT_FILENO);
	dup2(d, STDERR_FILENO);

	int pid = fork();
	if(pid == 0)
	{
		execv(argv0, parameters_vector);
		printf("ERROR, COULD NOT LAUNCH OFFERSTREAMER\n");
		exit(2);
	}
	else
		P2PProcessID = pid;
	
	// restore backup descriptors in the parent process
	dup2(stdoutS, STDOUT_FILENO);
	dup2(stderrS, STDERR_FILENO);
	
	for(i=0; i<par_count; i++)
		free(parameters_vector[i]);
		
	fclose(stream);

#ifdef RESTORE_SCREEN_ON_ZAPPING
	if(was_fullscreen)
		ChunkerPlayerGUI_ToggleFullscreen();
	else
	{
		ChunkerPlayerGUI_HandleResize(old_width, old_height);
	}
#endif
	
	ChunkerPlayerCore_Play();
	
	return 0;
#endif

	return 1;
}

void ZapDown()
{
	SelectedChannel = ((SelectedChannel+1) %NChannels);
	SwitchChannel(&(Channels[SelectedChannel]));
}

void ZapUp()
{
	SelectedChannel--;
	if(SelectedChannel < 0)
		SelectedChannel = NChannels-1;

	SwitchChannel(&(Channels[SelectedChannel]));
}

int enqueueBlock(const uint8_t *block, const int block_size)
{
	return ChunkerPlayerCore_EnqueueBlocks(block, block_size);
}
