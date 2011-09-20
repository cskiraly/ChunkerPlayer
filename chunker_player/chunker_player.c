/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include <stdio.h>
#include <unistd.h>
#include <SDL.h>
#include <SDL_mutex.h>
#include "player_defines.h"
#include <confuse.h>
#include "http_default_urls.h"
#include "player_defines.h"
#include "chunker_player.h"
#include "chunk_puller.h"
#include "player_gui.h"
#include <time.h>
#include <getopt.h>

#ifdef PSNR_PUBLICATION
#include <event2/event.h>
#include <napa_log.h>
#endif

#ifdef CHANNELS_DOWNLOAD
#include "http.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

int NChannels;
char StreamerFilename[255];
char *ConfFilename = NULL;
int Port;

int ParseConf(char *file, char *uri);
int SwitchChannel(SChannel* channel);

int ReadALine(FILE* fp, char* Output, int MaxOutputSize)
{
    int i=0;
    int c;
    do
    {
        c=getc(fp);
        if(c=='\r' || c=='\n' || c==EOF)
        {
            Output[i]=0;
            return i;
        }
        Output[i++]=c;
    }
    while(c!=EOF);
    
    return -1;
}

int CheckForRepoAddress(char* Param)
{
	int result = 0;
    int i, Found=0;

    if(strncasecmp(Param,"repo_address=",13)==0)
    {
        result=1;
        //find ',' in Param (if exist)
        int len=strlen(Param);
        char* tmp=(char*)calloc(1,len);
        if(tmp)
        {
            for(i=13;i<len;i++)
            {
                if(Param[i]==',')
                {
                    Found=1;
                    break;
                }
                tmp[i-13]=Param[i];
            }

            if(i==len || Found) strncpy(RepoAddress,tmp,2048);
            free(tmp);
        }
    }

	return result;
}

void sigproc()
{
	printf("you have pressed ctrl-c, terminating...\n");
	quit = 1;
}

static void print_usage(int argc, char *argv[])
{
  fprintf (stderr,
    "\nUsage:%s [options]\n"
    "\n"
    "\t[-c ChannelName]: channel name (from channels.conf)\n"
    "\t[-C file]: channel list file name (default: channels.conf)\n"
    "\t[-p port]: player http port\n"
    "\t[-q q_thresh]: playout queue size\n"
    "\t[-A audiocodec]\n"
    "\t[-V videocodec]\n"
    "\t[-t]: log traces (WARNING: old traces will be deleted).\n"
    "\t[-s mode]: silent mode (no GUI) (mode=1 audio ON, mode=2 audio OFF, mode=3 audio OFF; P2P OFF).\n\n"
    "=======================================================\n", argv[0]
    );
}

// initialize a receiver instance for receiving chunks from a Streamer process
// returns: <0 on error
int initIPCReceiver(Port)
{
#ifdef HTTPIO
	struct MHD_Daemon *daemon = NULL;
	//this thread fetches chunks from the network by listening to the following path, port
	daemon = (struct MHD_Daemon*)initChunkPuller(UL_DEFAULT_EXTERNALPLAYER_PATH, Port);
	if(daemon == NULL)
	{
		printf("CANNOT START MICROHTTPD SERVICE, EXITING...\n");
		return -1;
	}
#endif
#ifdef TCPIO
	int fd = initChunkPuller(Port);
	if(! (fd > 0))
	{
		printf("CANNOT START TCP PULLER...\n");
		return -1;
	}
#endif

	return 1;
}

int main(int argc, char *argv[])
{
	srand ( time(NULL) );
	// some initializations
	SilentMode = 0;
	queue_filling_threshold = 50;
	quit = 0;
	QueueFillingMode=1;
	LogTraces = 0;

	NChannels = 0;
	SelectedChannel = -1;
	char firstChannelName[255];
	int firstChannelIndex;
	
	firstChannelName[0] = 0;
	memset((void*)Channels, 0, (MAX_CHANNELS_NUM*sizeof(SChannel)));

	Port = 6100;

	SDL_Event event;
	OverlayMutex = SDL_CreateMutex();
	
	char c;
	while ((c = getopt (argc, argv, "q:c:C:p:s:t")) != -1)
	{
		switch (c) {
			case 0: //for long options
				break;
			case 'q':
				sscanf(optarg, "%d", &queue_filling_threshold);
				break;
			case 'c':
				sprintf(firstChannelName, "%s", optarg);
				break;
			case 'C':
				ConfFilename = strdup(optarg);
				break;
			case 'p':
				sscanf(optarg, "%d", &Port);
				break;
			case 's':
				sscanf(optarg, "%d", &SilentMode);
				break;
			case 't':
				DELETE_DIR("traces");
				CREATE_DIR("traces");
				LogTraces = 1;
				break;
			default:
				print_usage(argc, argv);
				return -1;
		}
	}

#ifdef EMULATE_CHUNK_LOSS
	ScheduledChunkLosses = NULL;
	cfg_opt_t scheduled_chunk_loss_opts[] =
	{
		CFG_INT("Time", 0, CFGF_NONE),
		CFG_INT("Value", 0, CFGF_NONE),
		CFG_INT("MinValue", 0, CFGF_NONE),
		CFG_INT("MaxValue", 0, CFGF_NONE),
		CFG_INT("Burstiness", 0, CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t opts[] =
	{
		CFG_SEC("ScheduledChunkLoss", scheduled_chunk_loss_opts, CFGF_MULTI),
		CFG_END()
	};
	cfg_t *cfg, *cfg_sched;
	cfg = cfg_init(opts, CFGF_NONE);
	if(!cfg_parse(cfg, "_chunklossrate.conf") == CFG_PARSE_ERROR)
	{
		NScheduledChunkLosses = cfg_size(cfg, "ScheduledChunkLoss");
		if(NScheduledChunkLosses > 0)
			ScheduledChunkLosses = (SChunkLoss*)malloc((NScheduledChunkLosses)*sizeof(SChunkLoss));
		
		int j;
		for(j = 0; j < cfg_size(cfg, "ScheduledChunkLoss"); j++)
		{
			cfg_sched = cfg_getnsec(cfg, "ScheduledChunkLoss", j);
			ScheduledChunkLosses[j].Time = cfg_getint(cfg_sched, "Time");
			ScheduledChunkLosses[j].Value = cfg_getint(cfg_sched, "Value");
			ScheduledChunkLosses[j].Burstiness = cfg_getint(cfg_sched, "Burstiness");
			
			// -1 means random value between min and max
			if(ScheduledChunkLosses[j].Value == -1)
			{
				ScheduledChunkLosses[j].MinValue = cfg_getint(cfg_sched, "MinValue");
				ScheduledChunkLosses[j].MaxValue = cfg_getint(cfg_sched, "MaxValue");
			}
		}
		cfg_free(cfg);
		CurrChunkLossIndex = -1;
		
		for(j=0; j < NScheduledChunkLosses; j++)
		{
			printf("ScheduledChunkLosses[%d].Time = %ld\n", j, ScheduledChunkLosses[j].Time);
			printf("ScheduledChunkLosses[%d].Value = %d\n", j, ScheduledChunkLosses[j].Value);
			printf("ScheduledChunkLosses[%d].Burstiness = %d\n", j, ScheduledChunkLosses[j].Burstiness);
		}
	}
#endif

	if (initIPCReceiver(Port) < 0) {
		exit(2);
	}

#ifdef PSNR_PUBLICATION
	repoclient=NULL;
	LastTimeRepoPublish.tv_sec=0;
	LastTimeRepoPublish.tv_usec=0;
	eventbase = event_base_new();
	napaInitLog(LOG_DEBUG, NULL, NULL);
	repInit("");
#endif

	if(SilentMode == 0)
	{
		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
			fprintf(stderr, "Could not initialize SDL audio/video or timer - %s\n", SDL_GetError());
			return -1;
		}
	}
	else if(SilentMode == 1)
	{
		if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
			fprintf(stderr, "Could not initialize SDL audio or timer - %s\n", SDL_GetError());
			return -1;
		}
	}
	else
	{
		if(SDL_Init(SDL_INIT_TIMER)) {
			fprintf(stderr, "Could not initialize SDL timer - %s\n", SDL_GetError());
			return -1;
		}
	}

	if (ConfFilename) {
		if(ParseConf(ConfFilename, NULL))
		{
			printf("ERROR: Cannot parse configuration file %s, exit...\n", ConfFilename);
			exit(1);
		}
	} else {
		if(ParseConf(DEFAULT_CONF_FILENAME, DEFAULT_CONF_URI))
		{
			printf("ERROR: Cannot parse configuration file, exit...\n");
			exit(1);
		}
	}

	firstChannelIndex = -1;
	int it;
	for(it = 0; it < NChannels; it++)
	{
		if(firstChannelName[0] == 0) {
			firstChannelIndex = 0;
			break;
		} else if (!strcmp(Channels[it].Title, firstChannelName))
		{
			firstChannelIndex = it;
			break;
		}
	}
	
	if(firstChannelIndex < 0)
	{
		printf("Cannot find the specified channel (%s) in the configuration file (channels.conf), exiting\n", firstChannelName);
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
		
#ifdef PSNR_PUBLICATION
		event_base_loop(eventbase, EVLOOP_NONBLOCK);
#endif

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
				case SDL_KEYDOWN:  /* Handle a KEYDOWN event */
					switch( event.key.keysym.sym ){
						case SDLK_ESCAPE:
							if(FullscreenMode) {
								ChunkerPlayerGUI_ToggleFullscreen();
							}
							break;
						case SDLK_r:
							ChunkerPlayerGUI_ChangeRatio();
							break;
						case SDLK_UP:
							ZapUp();
							break;
						case SDLK_DOWN:
							ZapDown();
							break;
						case SDLK_LEFT:
							ChunkerPlayerCore_ChangeDelay(100);
							break;
						case SDLK_RIGHT:
							ChunkerPlayerCore_ChangeDelay(-100);
							break;
						default:
							break;
					}
				break;
			}
			ChunkerPlayerGUI_HandleKey();
		}
		usleep(120000);
	}

	KILL_PROCESS(&(Channels[SelectedChannel].StreamerProcess));

	//TERMINATE
	ChunkerPlayerCore_Stop();
	ChunkerPlayerCore_Finalize();
	ChunkerPlayerGUI_Close();
	SDL_DestroyMutex(OverlayMutex);
	SDL_Quit();
	
#ifdef HTTPIO
	finalizeChunkPuller(daemon);
#endif
#ifdef TCPIO
	finalizeChunkPuller();
#endif
	
#ifdef EMULATE_CHUNK_LOSS
	if(ScheduledChunkLosses)
		free(ScheduledChunkLosses);
#endif

#ifdef PSNR_PUBLICATION
	if(repoclient) repClose(repoclient);	event_base_free(eventbase);
#endif
	return 0;
}

int cb_validate_conffile(cfg_t *cfg)
{
	char LaunchString[255];
	cfg_t *cfg_greet;
	
	if(cfg_size(cfg, "Channel") == 0)
	{
		cfg_error(cfg, "no \"Channel\" section found");
		return -1;
	}
	
	printf("\t%d Channel setions found\n", cfg_size(cfg, "Channel"));
	
	int j;
	for(j = 0; j < cfg_size(cfg, "Channel"); j++)
	{
		cfg_greet = cfg_getnsec(cfg, "Channel", j);
		sprintf(LaunchString, "%s", cfg_getstr(cfg_greet, "LaunchString"));
		if(!(strlen(LaunchString) > 0))
		{
			cfg_error(cfg, "invalid LaunchString for Channel[%d]", j);
			return -1;
		}
		printf("\tChannel[%d].LaunchString = %s\n", j, LaunchString);
		printf("\tChannel[%d].AudioChannels = %ld\n", j, cfg_getint(cfg_greet, "AudioChannels"));
		printf("\tChannel[%d].SampleRate = %ld\n", j, cfg_getint(cfg_greet, "SampleRate"));
		printf("\tChannel[%d].Width = %ld\n", j, cfg_getint(cfg_greet, "Width"));
		printf("\tChannel[%d].Height = %ld\n", j, cfg_getint(cfg_greet, "Height"));
		printf("\tChannel[%d].Bitrate = %ld\n", j, cfg_getint(cfg_greet, "Bitrate"));
		printf("\tChannel[%d].Ratio = %s\n", j, cfg_getstr(cfg_greet, "Ratio"));
	}
    return 0;
}

int ParseConf(char *file, char *uri)
{
	int j,r;
	char * conf = NULL;

	// PARSING CONF FILE
	cfg_opt_t channel_opts[] =
	{
		CFG_STR("Title", "", CFGF_NONE),
		CFG_STR("LaunchString", "", CFGF_NONE),
		CFG_INT("AudioChannels", 2, CFGF_NONE),
		CFG_INT("SampleRate", 48000, CFGF_NONE),
		CFG_INT("Width", 176, CFGF_NONE),
		CFG_INT("Height", 144, CFGF_NONE),
		CFG_INT("Bitrate", 0, CFGF_NONE),
		CFG_STR("VideoCodec", "mpeg4", CFGF_NONE),
		CFG_STR("AudioCodec", "mp3", CFGF_NONE),
		
		// for some reason libconfuse parsing for floating point does not work in windows
		//~ CFG_FLOAT("Ratio", 1.22, CFGF_NONE),
		CFG_STR("Ratio", "1.22", CFGF_NONE),
		CFG_END()
	};
	cfg_opt_t opts[] =
	{
		CFG_SEC("Channel", channel_opts, CFGF_TITLE | CFGF_MULTI),
		CFG_END()
	};
	cfg_t *cfg, *cfg_channel;
	cfg = cfg_init(opts, CFGF_NONE);

#ifdef CHANNELS_DOWNLOAD
	if (uri) {
		http_get2file(uri, file);
	}
#endif

	r = cfg_parse(cfg, file);
	if (r == CFG_PARSE_ERROR) {
		printf("Error while parsing configuration file, exiting...\n");
		cb_validate_conffile(cfg);
		return 1;
	} else if (r == CFG_FILE_ERROR) {
		printf("Error trying parsing configuration file. '%s' couldn't be opened for reading\n", file);
		return 1;
	}

	FILE * tmp_file;
	if( (tmp_file = fopen(DEFAULT_PEEREXECNAME_FILENAME, "r")) ) {
		if(fscanf(tmp_file, "%s", StreamerFilename) != 1) {
			printf("Wrong format of conf file %s containing peer application exec name. Assuming default: %s.\n\n", DEFAULT_PEEREXECNAME_FILENAME, DEFAULT_PEER_EXEC_NAME);
			strncpy(StreamerFilename, DEFAULT_PEER_EXEC_NAME, 255);
		}
		fclose(tmp_file);
	}
	else {
		printf("Could not find conf file %s containing peer application exec name. Exiting.\n\n", DEFAULT_PEEREXECNAME_FILENAME);
		exit(1);
	}
	if( (tmp_file = fopen(StreamerFilename, "r")) )
    {
        fclose(tmp_file);
    }
    else
	{
		printf("Could not find peer application (named '%s') into the current folder, please copy or link it into the player folder, then retry\n\n", StreamerFilename);
		exit(1);
	}
	
	for(j = 0; j < cfg_size(cfg, "Channel"); j++)
	{
		cfg_channel = cfg_getnsec(cfg, "Channel", j);
		sprintf(Channels[j].Title, "%s", cfg_title(cfg_channel));
		strcpy(Channels[j].LaunchString, cfg_getstr(cfg_channel, "LaunchString"));
		strcpy(Channels[j].VideoCodec, cfg_getstr(cfg_channel, "VideoCodec"));
		strcpy(Channels[j].AudioCodec, cfg_getstr(cfg_channel, "AudioCodec"));
		Channels[j].Width = cfg_getint(cfg_channel, "Width");
		Channels[j].Height = cfg_getint(cfg_channel, "Height");
		Channels[j].AudioChannels = cfg_getint(cfg_channel, "AudioChannels");
		Channels[j].SampleRate = cfg_getint(cfg_channel, "SampleRate");
		Channels[j].Ratio = strtof(cfg_getstr(cfg_channel, "Ratio"), 0);
		Channels[j].Bitrate = cfg_getint(cfg_channel, "Bitrate");
		
		Channels[j].Index = j+1;
		NChannels++;
	}
	cfg_free(cfg);

	return 0;
}

int ReTune(SChannel* channel)
{	
	if(ChunkerPlayerCore_IsRunning())
		ChunkerPlayerCore_Pause();
	
	//reset quality info
	channel->startTime = time(NULL);
	
	ChunkerPlayerCore_Play();
	
	return 0;
}

int StartStreamer(SChannel* channel)
{
	char argv0[255], parameters_string[511];
	sprintf(argv0, "%s", StreamerFilename);

#ifdef HTTPIO
	sprintf(parameters_string, "%s %s %s %d %s %s %d", "-C", channel->Title, "-P", (Port+channel->Index), channel->LaunchString, "-F", Port);
#endif

#ifdef TCPIO
	sprintf(parameters_string, "%s %s %s %d %s %s tcp://127.0.0.1:%d", "-C", channel->Title, "-P", (Port+channel->Index), channel->LaunchString, "-F", Port);
#endif

	printf("OFFERSTREAMER LAUNCH STRING: %s %s\n", argv0, parameters_string);

	if(SilentMode != 3) //mode 3 is without P2P peer process
	{

#ifndef __WIN32__
		int i;
		char* parameters_vector[255];
		parameters_vector[0] = argv0;

		RepoAddress[0]='\0';

		// split parameters and count them
		int par_count=1;
		char* pch = strtok (parameters_string, " ");
		while (pch != NULL)
		{
			if(par_count > 255) break;
			//printf ("\tpch=%s\n",pch);
			parameters_vector[par_count] = strdup(pch);
			// Find repo_address
			CheckForRepoAddress(parameters_vector[par_count]);
			pch = strtok (NULL, " ");
			par_count++;
		}
		parameters_vector[par_count] = NULL;

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
		//dup2(d, STDERR_FILENO);

		int pid = fork();
		if(pid == 0)
		{
			execv(argv0, parameters_vector);
			printf("ERROR, COULD NOT LAUNCH OFFERSTREAMER\n");
			exit(2);
		}
		else {
			channel->StreamerProcess = pid;
		}

		// restore backup descriptors in the parent process
		dup2(stdoutS, STDOUT_FILENO);
		dup2(stderrS, STDERR_FILENO);

		fclose(stream);
		for(i=1; i<par_count; i++)
			free(parameters_vector[i]);

#else
		STARTUPINFO sti;
		SECURITY_ATTRIBUTES sats = { 0 };
		int ret = 0;

		//set SECURITY_ATTRIBUTES struct fields
		sats.nLength = sizeof(sats);
		sats.bInheritHandle = TRUE;
		sats.lpSecurityDescriptor = NULL;

		ZeroMemory( &sti, sizeof(sti) );
		sti.cb = sizeof(sti);
		ZeroMemory( &channel->StreamerProcess, sizeof(PROCESS_INFORMATION) );

		char buffer[512];
		sprintf(buffer, "%s %s", argv0, parameters_string);

		if(!CreateProcess(NULL,
	  	buffer,
	  	&sats,
	  	&sats,
	  	TRUE,
	  	0,
	  	NULL,
	  	NULL,
	  	&sti,
		&channel->StreamerProcess))
		{
			printf("Unable to generate process \n");
			return -1;
		}
#endif
	}

	return 1;
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

#ifdef PSNR_PUBLICATION
	remove("NetworkID");
#endif
	
	ChunkerPlayerGUI_SetChannelRatio(channel->Ratio);
	ChunkerPlayerGUI_SetChannelTitle(channel->Title);
	ChunkerPlayerGUI_ForceResize(channel->Width, channel->Height);

	ChunkerPlayerCore_SetupOverlay(channel->Width, channel->Height);
	if(ChunkerPlayerCore_InitCodecs(channel->VideoCodec, channel->Width, channel->Height, channel->AudioCodec, channel->SampleRate, channel->AudioChannels) < 0)
	{
		printf("ERROR, COULD NOT INITIALIZE CODECS\n");
		exit(2);
	}
	
	//reset quality info
	channel->startTime = time(NULL);
	channel->instant_score = 0.0;
	channel->average_score = 0.0;
	channel->history_index = 0;
	for(i=0; i<CHANNEL_SCORE_HISTORY_SIZE; i++)
		channel->score_history[i] = -1;
	sprintf(channel->quality, "EVALUATING...");

	StartStreamer(channel);

#ifdef RESTORE_SCREEN_ON_ZAPPING
	if(SilentMode == 0) {
		if(was_fullscreen)
			ChunkerPlayerGUI_ToggleFullscreen();
		else
		{
			ChunkerPlayerGUI_HandleResize(old_width, old_height);
		}
	}
#endif

#ifdef PSNR_PUBLICATION
	if(RepoAddress[0]!='\0')	// TODO: search for RepoAddress currently disabled on linux
	{
	    // Open Repository
	    if(repoclient)
		    repClose(repoclient);
	    repoclient=NULL;

	    repoclient = repOpen(RepoAddress,0);
	    if (repoclient == NULL)
		    printf("Unable to initialize PSNR publication in repoclient %s\n", RepoAddress);
    }
    else {
	    printf("Repository address not present in streames launch string. Publication disabled\n");
    }
#endif


#ifdef PSNR_PUBLICATION
	// Read the Network ID
	int Error=true;
	char Line1[255], Line2[255];
	while(Error)
	{
	    FILE* fp=fopen("NetworkID","r");	//TODO: better error handling needed, this could block the player if there are no write permissions
	    if(fp)
	    {
	        if(ReadALine(fp,Line1,255)!=-1)
	            if(ReadALine(fp,Line2,255)!=-1)
	            {
	                if(strcmp(Line2,"IDEnd")==0)
	                {
	                    strcpy(NetworkID,Line1);
	                    Error=false;
	                }
	            }
	        fclose(fp);
	    }
	    if(Error) usleep(100000);
	}
	
	printf("NetworkID = %s\n",NetworkID);
#endif
	
	ChunkerPlayerCore_Play();
	ChunkerPlayerGUI_ChannelSwitched();
	return 0;
}

void ZapDown()
{
	KILL_PROCESS(&Channels[SelectedChannel].StreamerProcess);
	SelectedChannel = ((SelectedChannel+1) %NChannels);
	SwitchChannel(&(Channels[SelectedChannel]));
}

void ZapUp()
{
	KILL_PROCESS(&Channels[SelectedChannel].StreamerProcess);
	SelectedChannel--;
	if(SelectedChannel < 0)
		SelectedChannel = NChannels-1;

	SwitchChannel(&(Channels[SelectedChannel]));
}

int enqueueBlock(const uint8_t *block, const int block_size)
{
	return ChunkerPlayerCore_EnqueueBlocks(block, block_size);
}
