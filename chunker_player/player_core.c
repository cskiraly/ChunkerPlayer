#include "player_defines.h"
#include "chunker_player.h"
#include "player_gui.h"
#include "player_core.h"
#include <assert.h>

void SaveFrame(AVFrame *pFrame, int width, int height);
int VideoCallback(void *valthread);
void AudioCallback(void *userdata, Uint8 *stream, int len);
void UpdateQueueStats(PacketQueue *q, int packet_index);
void UpdateLossTraces(int type, int first_lost, int n_lost);
int UpdateQualityEvaluation(double instant_lost_frames, double instant_skips, int do_update);
void PacketQueueCleanStats(PacketQueue *q);

void PacketQueueInit(PacketQueue *q, short int Type)
{
#ifdef DEBUG_QUEUE
	printf("QUEUE: INIT BEGIN: NPackets=%d Type=%s\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
	memset(q,0,sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	QueueFillingMode=1;
	q->queueType=Type;
	q->last_frame_extracted = -1;
	q->total_lost_frames = 0;
	q->instant_lost_frames = 0;
	q->total_skips = 0;
	q->last_skips = 0;
	q->instant_skips = 0;
	q->first_pkt= NULL;
	//q->last_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	q->density= 0.0;
	FirstTime = 1;
	FirstTimeAudio = 1;
	//init up statistcs
	PacketQueueCleanStats(q);
#ifdef DEBUG_QUEUE
	printf("QUEUE: INIT END: NPackets=%d Type=%s\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
}

void PacketQueueReset(PacketQueue *q)
{
	AVPacketList *tmp,*tmp1;
#ifdef DEBUG_QUEUE
	printf("QUEUE: RESET BEGIN: NPackets=%d Type=%s LastExtr=%d\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->last_frame_extracted);
#endif
	SDL_LockMutex(q->mutex);

	tmp = q->first_pkt;
	while(tmp) {
		tmp1 = tmp;
		tmp = tmp->next;
		av_free_packet(&(tmp1->pkt));
		av_free(tmp1);
#ifdef DEBUG_QUEUE
		printf("F ");
#endif
		q->total_lost_frames++;
	}
#ifdef DEBUG_QUEUE
	printf("\n");
#endif

	QueueFillingMode=1;
	q->last_frame_extracted = -1;
	// on queue reset do not reset loss count
	// (loss count reset is done on queue init, ie channel switch)
	// q->total_lost_frames = 0;
	q->density=0.0;
	q->first_pkt= NULL;
	//q->last_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	FirstTime = 1;
	FirstTimeAudio = 1;
	//clean up statistcs
	PacketQueueCleanStats(q);
#ifdef DEBUG_QUEUE
	printf("QUEUE: RESET END: NPackets=%d Type=%s LastExtr=%d\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->last_frame_extracted);
#endif
	SDL_UnlockMutex(q->mutex);
}

void PacketQueueCleanStats(PacketQueue *q) {
	int i=0;
	for(i=0; i<LOSS_HISTORY_MAX_SIZE; i++) {
		q->skip_history[i] = -1;
		q->loss_history[i] = -1;
	}
	q->loss_history_index = 0;
	q->skip_history_index = 0;
	q->instant_skips = 0.0;
	q->instant_lost_frames = 0.0;
	q->instant_window_size = 50; //averaging window size, self-correcting based on window_seconds
	q->instant_window_size_target = 0;
	q->instant_window_seconds = 1; //we want to compute number of events in a 1sec wide window
	q->last_window_size_update = 0;
}

int ChunkerPlayerCore_PacketQueuePut(PacketQueue *q, AVPacket *pkt)
{
	short int skip = 0;
	AVPacketList *pkt1, *tmp, *prevtmp;

	if(q->nb_packets > queue_filling_threshold*QUEUE_MAX_GROW_FACTOR) {
#ifdef DEBUG_QUEUE
		printf("QUEUE: PUT i have TOO MANY packets %d Type=%s, RESETTING\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
		PacketQueueReset(q);
  }

	//make a copy of the incoming packet
	if(av_dup_packet(pkt) < 0) {
#ifdef DEBUG_QUEUE
		printf("QUEUE: PUT in Queue cannot duplicate in packet	: NPackets=%d Type=%s\n",q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
		return -1;
	}
	pkt1 = av_malloc(sizeof(AVPacketList));

	if(!pkt1) {
		av_free_packet(pkt);
		return -1;
	}
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

	// INSERTION SORT ALGORITHM
	// before inserting pkt, check if pkt.stream_index is <= current_extracted_frame.
	if(pkt->stream_index > q->last_frame_extracted) {
		// either checking starting from the first_pkt or needed other struct like AVPacketList with next and prev....
		//if (!q->last_pkt)
		if(!q->first_pkt) {
			q->first_pkt = pkt1;
			q->last_pkt = pkt1;
		}
		else if(pkt->stream_index < q->first_pkt->pkt.stream_index) {
			//the packet that has arrived is earlier than the first we got some time ago!
			//we need to put it at the head of the queue
			pkt1->next = q->first_pkt;
			q->first_pkt = pkt1;
		}
		else {
			tmp = q->first_pkt;
			while(tmp->pkt.stream_index < pkt->stream_index) {
				prevtmp = tmp;
				tmp = tmp->next;

				if(!tmp) {
					break;
				}
			}
			if(tmp && tmp->pkt.stream_index == pkt->stream_index) {
				//we already have a frame with that index
				skip = 1;
#ifdef DEBUG_QUEUE
				printf("QUEUE: PUT: we already have frame with index %d, skipping\n", pkt->stream_index);
#endif
			}
			else {
				prevtmp->next = pkt1;
				pkt1->next = tmp;
				if(pkt1->next == NULL)
					q->last_pkt = pkt1;
			}
			//q->last_pkt->next = pkt1; // It was uncommented when not insertion sort
		}
		if(skip == 0) {
			//q->last_pkt = pkt1;
			q->nb_packets++;
			q->size += pkt1->pkt.size;
			if(q->nb_packets>=queue_filling_threshold && QueueFillingMode) // && q->queueType==AUDIO)
			{
				QueueFillingMode=0;
#ifdef DEBUG_QUEUE
				printf("QUEUE: PUT: FillingMode set to zero\n");
#endif
			}
		}
	}

	else {
		av_free_packet(&pkt1->pkt);
		av_free(pkt1);
#ifdef DEBUG_QUEUE
				printf("QUEUE: PUT: NOT inserting because index %d > last extracted %d\n", pkt->stream_index, q->last_frame_extracted);
#endif
	}

	// minus one means no lost frames estimation, useless during QueuePut operations
	UpdateQueueStats(q, -1);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

int ChunkerPlayerCore_InitCodecs(int width, int height, int sample_rate, short int audio_channels)
{
	// some initializations
	QueueStopped = 0;
	AudioQueueOffset=0;
	AVPlaying = 0;
	GotSigInt = 0;
	FirstTimeAudio=1;
	FirstTime = 1;
	deltaAudioQError=0;
	InitRect = NULL;
	img_convert_ctx = NULL;
	
	SDL_AudioSpec wanted_spec;
	AVCodec         *aCodec;
	
	memset(&VideoCallbackThreadParams, 0, sizeof(ThreadVal));
	
	VideoCallbackThreadParams.width = width;
	VideoCallbackThreadParams.height = height;

	// Register all formats and codecs
	av_register_all();

	aCodecCtx = avcodec_alloc_context();
	//aCodecCtx->bit_rate = 64000;
	aCodecCtx->sample_rate = sample_rate;
	aCodecCtx->channels = audio_channels;
#ifdef MP3_AUDIO_ENCODER
	aCodec = avcodec_find_decoder(CODEC_ID_MP3); // codec audio
#else
	aCodec = avcodec_find_decoder(CODEC_ID_MP2);
#endif
	printf("MP2 codec id %d MP3 codec id %d\n",CODEC_ID_MP2,CODEC_ID_MP3);
	if(!aCodec) {
		printf("Codec not found!\n");
		return -1;
	}
	if(avcodec_open(aCodecCtx, aCodec)<0) {
		fprintf(stderr, "could not open codec\n");
		return -1; // Could not open codec
	}
	printf("using audio Codecid: %d ",aCodecCtx->codec_id);
	printf("samplerate: %d ",aCodecCtx->sample_rate);
	printf("channels: %d\n",aCodecCtx->channels);
	wanted_spec.freq = aCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = aCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec.callback = AudioCallback;
	wanted_spec.userdata = aCodecCtx;
	if(SDL_OpenAudio(&wanted_spec,&AudioSpecification)<0)
	{
		fprintf(stderr,"SDL_OpenAudio: %s\n",SDL_GetError());
		return -1;
	}
	dimAudioQ = AudioSpecification.size;
	deltaAudioQ = (float)((float)AudioSpecification.samples)*1000/AudioSpecification.freq;

#ifdef DEBUG_AUDIO
	printf("freq:%d\n",AudioSpecification.freq);
	printf("format:%d\n",AudioSpecification.format);
	printf("channels:%d\n",AudioSpecification.channels);
	printf("silence:%d\n",AudioSpecification.silence);
	printf("samples:%d\n",AudioSpecification.samples);
	printf("size:%d\n",AudioSpecification.size);
	printf("deltaAudioQ: %f\n",deltaAudioQ);
#endif

	outbuf_audio = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

	//initialize the audio and the video queues
	PacketQueueInit(&audioq, AUDIO);
	PacketQueueInit(&videoq, VIDEO);
	
	// Init audio and video buffers
	av_init_packet(&AudioPkt);
	av_init_packet(&VideoPkt);
	//printf("AVCODEC_MAX_AUDIO_FRAME_SIZE=%d\n", AVCODEC_MAX_AUDIO_FRAME_SIZE);
	AudioPkt.data=(uint8_t *)malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(!AudioPkt.data) return 1;
	VideoPkt.data=(uint8_t *)malloc(width*height*3/2);
	if(!VideoPkt.data) return 1;
	
	InitRect = (SDL_Rect*) malloc(sizeof(SDL_Rect));
	if(!InitRect)
	{
		printf("Memory error!!!\n");
		return -1;
	}
	InitRect->x = OverlayRect.x;
	InitRect->y = OverlayRect.y;
	InitRect->w = OverlayRect.w;
	InitRect->h = OverlayRect.h;
	
	char audio_stats[255], video_stats[255];
	sprintf(audio_stats, "waiting for incoming audio packets...");
	sprintf(video_stats, "waiting for incoming video packets...");
	ChunkerPlayerGUI_SetStatsText(audio_stats, video_stats);
	
	return 0;
}

int DecodeEnqueuedAudio(AVPacket *pkt, PacketQueue *q)
{
	uint16_t *audio_bufQ = NULL;
	int16_t *dataQ = NULL;
	int data_sizeQ = AVCODEC_MAX_AUDIO_FRAME_SIZE;
	int lenQ;
	int ret = 0;

	//set the flag to decoded anyway	
	pkt->convergence_duration = -1;

	audio_bufQ = (uint16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(audio_bufQ) {
#ifdef DEBUG_AUDIO_BUFFER
		printf("AUDIO_BUFFER: about to decode packet %d, size %d, data %d\n", pkt->stream_index, pkt->size, pkt->data);
#endif
		//decode the packet data
		lenQ = avcodec_decode_audio3(aCodecCtx, (int16_t *)audio_bufQ, &data_sizeQ, pkt);
		if(lenQ > 0) {
			dataQ = (int16_t *)av_malloc(data_sizeQ); //this will be free later at the time of playback
			if(dataQ) {
				memcpy(dataQ, audio_bufQ, data_sizeQ);
				if(pkt->data != NULL)
				{
					//discard the old encoded bytes
					av_free(pkt->data);
				}
				//subtract them from queue size
				q->size -= pkt->size;
				pkt->data = (int8_t *)dataQ;
				pkt->size = data_sizeQ;
				//add new size to queue size
				q->size += pkt->size;
				ret = 1;
			}
			else {
#ifdef DEBUG_AUDIO_BUFFER
				printf("AUDIO_BUFFER: cannot alloc space for decoded packet %d\n", pkt->stream_index);
#endif
			}
		}
		else {
#ifdef DEBUG_AUDIO_BUFFER
			printf("AUDIO_BUFFER: cannot decode packet %d\n", pkt->stream_index);
#endif
		}
		av_free(audio_bufQ);
	}
	else {
#ifdef DEBUG_AUDIO_BUFFER
		printf("AUDIO_BUFFER: cannot alloc decode buffer for packet %d\n", pkt->stream_index);
#endif
	}
	return ret; //problems occurred
}

/**
 * removes a packet from the list and returns the next
 * */
AVPacketList *RemoveFromQueue(PacketQueue *q, AVPacketList *p)
{
	AVPacketList *retpk = p->next;
	q->nb_packets--;
	//adjust size here and not in the various cases of the dequeue
	q->size -= p->pkt.size;
	if(&p->pkt)
		av_free_packet(&p->pkt);
	if(p)
		av_free(p);
	return retpk;
}

AVPacketList *SeekAndDecodePacketStartingFrom(AVPacketList *p, PacketQueue *q)
{
	while(p) {
			//check if audio packet has been already decoded
			if(p->pkt.convergence_duration == 0) {
				//not decoded yet, try to decode it
				if( !DecodeEnqueuedAudio(&(p->pkt), q) ) {
					//it was not possible to decode this packet, return next one
					p = RemoveFromQueue(q, p);
				}
				else
					return p;
			}
			else
				return p;
	}
	return NULL;
}

void UpdateQueueStats(PacketQueue *q, int packet_index)
{
	//remember that all static variables are shared betwee audio and video queues!!
	static int last_print = 0;

	//used as flag also (0 means dont update quality estimation average)
	int update_quality_avg = 0;
	int i;

	if(q == NULL)
		return;
	if(q->first_pkt == NULL)
		return;
	if(q->last_pkt == NULL)
		return;

	int now = time(NULL);
	if(!last_print)
		last_print = now;
	if(!q->last_window_size_update)
		q->last_window_size_update = now;

	//calculate the queue density both in case of QueuePut and QueueGet
	if(q->last_pkt->pkt.stream_index >= q->first_pkt->pkt.stream_index)
	{
		q->density = (double)q->nb_packets / (double)(q->last_pkt->pkt.stream_index - q->first_pkt->pkt.stream_index + 1) * 100.0; //plus 1 because if they are adjacent (difference 1) there really should be 2 packets in the queue
	}
#ifdef DEBUG_STATS
	if(q->queueType == AUDIO)
		printf("STATS: AUDIO QUEUE DENSITY percentage %f, last %d, first %d, pkts %d\n", q->density, q->last_pkt->pkt.stream_index, q->first_pkt->pkt.stream_index, q->nb_packets);
	if(q->queueType == VIDEO)
		printf("STATS: VIDEO QUEUE DENSITY percentage %f, last %d, first %d, pkts %d\n", q->density, q->last_pkt->pkt.stream_index, q->first_pkt->pkt.stream_index, q->nb_packets);
#endif

	//adjust the sliding window_size according to the elapsed time
	update_quality_avg = 0;
	//dynamically self-adjust the instant_window_size based on the time passing
	//to match it at our best, since we have no separate thread to count time
	if((now - q->last_window_size_update) >= q->instant_window_seconds) {
		int i = 0;
		//if window is enlarged, erase old samples
		for(i=q->instant_window_size; i<q->instant_window_size_target; i++) {
			q->skip_history[i] = -1;
			q->loss_history[i] = -1;
		}
#ifdef DEBUG_STATS
		printf("STATS: %s WINDOW_SIZE instant set from %d to %d\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->instant_window_size, q->instant_window_size_target);
#endif
		q->instant_window_size = q->instant_window_size_target;
		if(q->instant_window_size == 0) { //in case of anomaly reset to default
			q->instant_window_size = 50;
			printf("STATS: %s WINDOW_SIZE instant anomaly reset to default %d\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->instant_window_size);
		}
		if(q->instant_window_size > LOSS_HISTORY_MAX_SIZE) {
			printf("ERROR: %s instant_window size updated to %d, which is more than the max of %d. Exiting.\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->instant_window_size, LOSS_HISTORY_MAX_SIZE);
			exit(1);
		}
		q->instant_window_size_target = 0;
#ifdef DEBUG_STATS
		printf("STATS: %s WINDOW_SIZE instant moving time from %d to %d\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->last_window_size_update, now);
#endif
		//update time passing
		q->last_window_size_update = now;
		//signal to give the update values to the UpdateQuality evaluation
		//since we dont update long-term averaging every time, just do it at every window_seconds
		update_quality_avg = 1;
	}

	//calculate lost frames and skipped frames during playing only if we are called from a QueueGet
	//averaging them in a short-sized time window
	if(packet_index != -1)
	{
		int real_window_size = 0;
		int lost_frames = 0;
		double percentage = 0.0;	

		//self adjust window size
		q->instant_window_size_target++;

		//compute lost frame statistics
		if(q->last_frame_extracted > 0 && packet_index > q->last_frame_extracted)
		{
			lost_frames = packet_index - q->last_frame_extracted - 1;
			q->total_lost_frames += lost_frames;
			percentage = (double)q->total_lost_frames / (double)q->last_frame_extracted * 100.0;

			//save a trace of lost frames to file
			//we have lost "lost_frames" frames starting from the last extracted (excluded of course)
			UpdateLossTraces(q->queueType, q->last_frame_extracted+1, lost_frames);

			//compute the frame loss rate inside the short-sized sliding window
			q->loss_history[q->loss_history_index] = lost_frames;
			q->loss_history_index = (q->loss_history_index+1)%q->instant_window_size;
			q->instant_lost_frames = 0.0;
			real_window_size = 0;
#ifdef DEBUG_STATS_DEEP
			printf("STATS: QUALITY: %s UPDATE LOSS:", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
			for(i=0; i<q->instant_window_size; i++) {
				//-1 means not initialized value or erased value due to window shrinking
				if(q->loss_history[i] != -1) {
					real_window_size++;
					q->instant_lost_frames += (double)q->loss_history[i];
#ifdef DEBUG_STATS_DEEP
					printf(" %d", q->loss_history[i]);
#endif
				}
#ifdef DEBUG_STATS_DEEP
				else
					printf(" *");
#endif
			}
#ifdef DEBUG_STATS_DEEP
			printf("\n");
#endif
			q->instant_lost_frames /= (double)real_window_size; //average in the window
			q->instant_lost_frames /= (double)q->instant_window_seconds; //express them in events/sec
#ifdef DEBUG_STATS
			if(q->queueType == AUDIO)
				printf("STATS: AUDIO FRAMES LOST: instant %f, total %d, total percentage %f\n", q->instant_lost_frames, q->total_lost_frames, percentage);
			else if(q->queueType == VIDEO)
				printf("STATS: VIDEO FRAMES LOST: instant %f, total %d, total percentage %f\n", q->instant_lost_frames, q->total_lost_frames, percentage);

			printf("STATS: QUALITY: %s UPDATE LOSS instant window %d, real window %d\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->instant_window_size, real_window_size);
#endif
		}

		//compute the skip events inside the short-sized sliding window
		int skips = q->total_skips - q->last_skips;
		q->last_skips = q->total_skips;
		q->skip_history[q->skip_history_index] = skips;
		q->skip_history_index = (q->skip_history_index+1)%q->instant_window_size;
		q->instant_skips = 0.0;
		real_window_size = 0;
#ifdef DEBUG_STATS_DEEP
		printf("STATS: QUALITY: %s UPDATE SKIP:", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif
		for(i=0; i<q->instant_window_size; i++) {
			//-1 means not initialized value or erased value due to window shrinking
			if(q->skip_history[i] != -1) {
				real_window_size++;
				q->instant_skips += (double)q->skip_history[i];
#ifdef DEBUG_STATS_DEEP
				printf(" %d", q->skip_history[i]);
#endif
			}
#ifdef DEBUG_STATS_DEEP
			else
				printf(" *");
#endif
		}
#ifdef DEBUG_STATS_DEEP
		printf("\n");
#endif
		q->instant_skips /= (double)real_window_size; //average in the window
		q->instant_skips /= (double)q->instant_window_seconds; //express them in events/sec
#ifdef DEBUG_STATS
		if(q->queueType == AUDIO)
			printf("STATS: AUDIO SKIPS: instant %f, total %d\n", q->instant_skips, q->total_skips);
		else if(q->queueType == VIDEO)
			printf("STATS: VIDEO SKIPS: instant %f, total %d\n", q->instant_skips, q->total_skips);

		printf("STATS: QUALITY: %s UPDATE SKIP instant window %d, real window %d\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->instant_window_size, real_window_size);
#endif
	}

	//continuous estimate of the channel quality
	//but print it only if it has changed since last time
	if(UpdateQualityEvaluation(q->instant_lost_frames, q->instant_skips, update_quality_avg)) {
		//display channel quality
		char stats[255];
		sprintf(stats, "%s - %s", Channels[SelectedChannel].Title, Channels[SelectedChannel].quality);
		ChunkerPlayerGUI_SetChannelTitle(stats);
	}

	//now, every 1 second print the statistics on the player window GUI
	if((now-last_print) >= 1)
	{
		double instant_events_per_sec = 0.0;
		char stats[255];
		if(q->queueType == AUDIO)
		{
			sprintf(stats, "[AUDIO] %d\%% qdensity - %d lost_frames/sec - %d lost_frames - %d skips/sec - %d skips", (int)q->density, (int)q->instant_lost_frames, q->total_lost_frames, (int)q->instant_skips, q->total_skips);
			ChunkerPlayerGUI_SetStatsText(stats, NULL);
		}
		else if(q->queueType == VIDEO)
		{
			sprintf(stats, "[VIDEO] %d\%% qdensity - %d lost_frames/sec - %d lost_frames - %d skips/sec - %d skips", (int)q->density, (int)q->instant_lost_frames, q->total_lost_frames, (int)q->instant_skips, q->total_skips);
			ChunkerPlayerGUI_SetStatsText(NULL, stats);
		}
		//update time passing
		last_print = now;
	}
}

//returns 1 if quality value has changed since last time
int UpdateQualityEvaluation(double instant_lost_frames, double instant_skips, int do_update)
{
	//as of now, we enter new samples in the long-term averaging once every sec,
	//thus 120 samples worth of window size equals 2 minutes averaging
	static int avg_window_size = 10; //averaging window size, self-correcting based on window_seconds
	static int avg_window_size_target = 0;
	static int avg_window_seconds = 10; //we want to compute number of events in a 2min wide window

	static int last_window_size_update = 0;

	char base_quality[255];
	char quality[255];
	int i;
	int now = time(NULL);
	if(!last_window_size_update)
		last_window_size_update = now;
	int runningTime = now - Channels[SelectedChannel].startTime;

	if(runningTime <= 0) {
		runningTime = 1;
#ifdef DEBUG_STATS
		printf("STATS: QUALITY warning channel runningTime %d. Set to one!\n", runningTime);
#endif
	}

	//update continuously the quality score
	Channels[SelectedChannel].instant_score = instant_skips + instant_lost_frames;

	if(do_update) {
#ifdef DEBUG_STATS
		printf("STATS: QUALITY: UPDATE SCORE lost frames %f, skips %f\n", instant_lost_frames, instant_skips);
#endif
		//every once in a while also enter samples in the long-term averaging window
		Channels[SelectedChannel].score_history[Channels[SelectedChannel].history_index] = Channels[SelectedChannel].instant_score;
		Channels[SelectedChannel].history_index = (Channels[SelectedChannel].history_index+1)%avg_window_size;

		Channels[SelectedChannel].average_score = 0.0;
		int real_window_size = 0;
		for(i=0; i<avg_window_size; i++) {
			//-1 means not initialized value or erased value due to window shrinking
			if(Channels[SelectedChannel].score_history[i] != -1) {
				real_window_size++;
				Channels[SelectedChannel].average_score += Channels[SelectedChannel].score_history[i];
			}
		}
		Channels[SelectedChannel].average_score /= (double)real_window_size; //average in the window
		Channels[SelectedChannel].average_score /= (double)avg_window_seconds; //express it in events/sec
#ifdef DEBUG_STATS
		printf("STATS: QUALITY: UPDATE SCORE avg window %d, real window %d\n", avg_window_size, real_window_size);
#endif
		//whenever we enter a sample, enlarge the self-adjusting window target (it will be checked later on)
		avg_window_size_target++;
	}
#ifdef DEBUG_STATS
	printf("STATS: QUALITY: instant skips %f, instant loss %f\n", instant_skips, instant_lost_frames);
	printf("STATS: QUALITY: instant score %f, avg score %f\n", Channels[SelectedChannel].instant_score, Channels[SelectedChannel].average_score);
#endif

	if(Channels[SelectedChannel].average_score > 0.02) {
		sprintf(base_quality, "POOR");
		if(Channels[SelectedChannel].instant_score < Channels[SelectedChannel].average_score) {
			sprintf(quality, "%s, GETTING BETTER", base_quality);
		}
		else if(Channels[SelectedChannel].instant_score == Channels[SelectedChannel].average_score) {
			sprintf(quality, "%s, STABLE", base_quality);
		}
		else {
			sprintf(quality, "%s, GETTING WORSE", base_quality);
		}
	}
	else {
		sprintf(base_quality, "GOOD");
		if(Channels[SelectedChannel].instant_score < Channels[SelectedChannel].average_score) {
			sprintf(quality, "%s, GETTING BETTER", base_quality);
		}
		else if(Channels[SelectedChannel].instant_score == Channels[SelectedChannel].average_score) {
			sprintf(quality, "%s, STABLE", base_quality);
		}
		else {
			sprintf(quality, "%s, GETTING WORSE", base_quality);
		}
	}

#ifdef DEBUG_STATS
	printf("STATS: QUALITY %s\n", quality);
#endif

	//dynamically self-adjust the instant_window_size based on the time passing
	//to match it at our best, since we have no separate thread to count time
	if((now - last_window_size_update) >= avg_window_seconds) {
		int i = 0;
		//if window is enlarged, erase old samples
		for(i=avg_window_size; i<avg_window_size_target; i++) {
			Channels[SelectedChannel].score_history[i] = -1;
		}
		avg_window_size = avg_window_size_target;
#ifdef DEBUG_STATS
		printf("STATS: WINDOW_SIZE avg set to %d\n", avg_window_size);
#endif
		if(avg_window_size > CHANNEL_SCORE_HISTORY_SIZE) {
			printf("ERROR: avg_window size updated to %d, which is more than the max of %d. Exiting.\n", avg_window_size, CHANNEL_SCORE_HISTORY_SIZE);
			exit(1);
		}
		avg_window_size_target = 0;
		//update time passing
		last_window_size_update = now;
	}

	if( strcmp(Channels[SelectedChannel].quality, quality) ) {
		//quality estimate has changed
		sprintf(Channels[SelectedChannel].quality, "%s", quality);
		return 1;
	}
	else {
		return 0;
	}
}

void UpdateLossTraces(int type, int first_lost, int n_lost)
{
	FILE *lossFile;
	int i;

	// Open loss traces file
	char filename[255];
	if(type == AUDIO)
		sprintf(filename, "audio_%s", LossTracesFilename);
	else
		sprintf(filename, "video_%s", LossTracesFilename);

	lossFile=fopen(filename, "a");
	if(lossFile==NULL) {
		printf("STATS: UNABLE TO OPEN Loss FILE: %s\n", filename);
		return;
	}

	for(i=0; i<n_lost; i++) {
		fprintf(lossFile, "%d\n", first_lost+i);
	}

	fclose(lossFile);
}

int PacketQueueGet(PacketQueue *q, AVPacket *pkt, short int av) {
	//AVPacket tmp;
	AVPacketList *pkt1 = NULL;
	int ret=-1;
	int SizeToCopy=0;

	SDL_LockMutex(q->mutex);

#ifdef DEBUG_QUEUE
	printf("QUEUE: Get NPackets=%d Type=%s\n", q->nb_packets, (q->queueType==AUDIO) ? "AUDIO" : "VIDEO");
#endif

	if((q->queueType==AUDIO && QueueFillingMode) || QueueStopped)
	{
		SDL_UnlockMutex(q->mutex);
		return -1;
	}

	if(av==1) { //somebody requested an audio packet, q is the audio queue
		//try to dequeue the first packet of the audio queue
		pkt1 = SeekAndDecodePacketStartingFrom(q->first_pkt, q);
		if(pkt1) { //yes we have them!
			if(pkt1->pkt.size-AudioQueueOffset > dimAudioQ) {
				//one packet if enough to give us the requested number of bytes by the audio_callback
#ifdef DEBUG_QUEUE_DEEP
				printf("  AV=1 and Extract from the same packet\n");
#endif
				pkt->size = dimAudioQ;
				memcpy(pkt->data,pkt1->pkt.data+AudioQueueOffset,dimAudioQ);
				pkt->dts = pkt1->pkt.dts;
				pkt->pts = pkt1->pkt.pts;
				pkt->stream_index = pkt1->pkt.stream_index;//1;
				pkt->flags = 1;
				pkt->pos = -1;
				pkt->convergence_duration = -1;
#ifdef DEBUG_QUEUE_DEEP
				printf("   Adjust timestamps Old = %lld New = %lld\n", pkt1->pkt.dts, (int64_t)(pkt1->pkt.dts + deltaAudioQ + deltaAudioQError));
#endif
				int64_t Olddts=pkt1->pkt.dts;
				pkt1->pkt.dts += deltaAudioQ + deltaAudioQError;
				pkt1->pkt.pts += deltaAudioQ + deltaAudioQError;
				deltaAudioQError=(float)Olddts + deltaAudioQ + deltaAudioQError - (float)pkt1->pkt.dts;
				AudioQueueOffset += dimAudioQ;
#ifdef DEBUG_QUEUE_DEEP
				printf("   deltaAudioQError = %f\n",deltaAudioQError);
#endif
				//update overall state of queue
				//size is diminished because we played some audio samples
				//but packet is not removed since a portion has still to be played
				//HINT ERRATA we had a size mismatch since size grows with the
				//number of compressed bytes, and diminishes here with the number
				//of raw uncompressed bytes, hence we update size during the
				//real removes and not here anymore
				//q->size -= dimAudioQ;
				UpdateQueueStats(q, pkt->stream_index);
				//update index of last frame extracted
				q->last_frame_extracted = pkt->stream_index;
#ifdef DEBUG_AUDIO_BUFFER
				printf("1: idx %d    \taqo %d    \tstc %d    \taqe %f    \tpsz %d\n", pkt1->pkt.stream_index, AudioQueueOffset, SizeToCopy, deltaAudioQError, pkt1->pkt.size);
#endif
				ret = 1; //OK
			}
			else {
				//we need bytes from two consecutive packets to satisfy the audio_callback
#ifdef DEBUG_QUEUE_DEEP
				printf("  AV = 1 and Extract from 2 packets\n");
#endif
				//check for a valid next packet since we will finish the current packet
				//and also take some bytes from the next one
				pkt1->next = SeekAndDecodePacketStartingFrom(pkt1->next, q);
				if(pkt1->next) {
#ifdef DEBUG_QUEUE_DEEP
					printf("   we have a next...\n");
#endif
					pkt->size = dimAudioQ;
					pkt->dts = pkt1->pkt.dts;
					pkt->pts = pkt1->pkt.pts;
					pkt->stream_index = pkt1->pkt.stream_index;//1;
					pkt->flags = 1;
					pkt->pos = -1;
					pkt->convergence_duration = -1;
					{
						SizeToCopy=pkt1->pkt.size-AudioQueueOffset;
#ifdef DEBUG_QUEUE_DEEP
						printf("      SizeToCopy=%d\n",SizeToCopy);
#endif
						memcpy(pkt->data, pkt1->pkt.data+AudioQueueOffset, SizeToCopy);
						memcpy(pkt->data+SizeToCopy, pkt1->next->pkt.data, (dimAudioQ-SizeToCopy)*sizeof(uint8_t));
					}
#ifdef DEBUG_AUDIO_BUFFER
					printf("2: idx %d    \taqo %d    \tstc %d    \taqe %f    \tpsz %d\n", pkt1->pkt.stream_index, AudioQueueOffset, SizeToCopy, deltaAudioQError, pkt1->pkt.size);
#endif
				}
#ifdef DEBUG_AUDIO_BUFFER
				else {
					printf("2: NONEXT\n");
				}
#endif
				//HINT SEE before q->size -= SizeToCopy;
				q->first_pkt = RemoveFromQueue(q, pkt1);

				// Adjust timestamps
				pkt1 = q->first_pkt;
				if(pkt1) {
					int Offset=(dimAudioQ-SizeToCopy)*1000/(AudioSpecification.freq*2*AudioSpecification.channels);
					int64_t LastDts=pkt1->pkt.dts;
					pkt1->pkt.dts += Offset + deltaAudioQError;
					pkt1->pkt.pts += Offset + deltaAudioQError;
					deltaAudioQError = (float)LastDts + (float)Offset + deltaAudioQError - (float)pkt1->pkt.dts;
#ifdef DEBUG_QUEUE_DEEP
					printf("   Adjust timestamps Old = %lld New = %lld\n", LastDts, pkt1->pkt.dts);
#endif
					AudioQueueOffset = dimAudioQ - SizeToCopy;
					//SEE BEFORE HINT q->size -= AudioQueueOffset;
					ret = 1;
					UpdateQueueStats(q, pkt->stream_index);
				}
				else {
					AudioQueueOffset=0;
				}
#ifdef DEBUG_QUEUE_DEEP
				printf("   deltaAudioQError = %f\n",deltaAudioQError);
#endif
				//update index of last frame extracted
				q->last_frame_extracted = pkt->stream_index;
			}
		}
	}
	else { //somebody requested a video packet, q is the video queue
		pkt1 = q->first_pkt;
		if(pkt1) {
#ifdef DEBUG_QUEUE_DEEP
			printf("  AV not 1\n");
#endif
			pkt->size = pkt1->pkt.size;
			pkt->dts = pkt1->pkt.dts;
			pkt->pts = pkt1->pkt.pts;
			pkt->stream_index = pkt1->pkt.stream_index;
			pkt->flags = pkt1->pkt.flags;
			pkt->pos = pkt1->pkt.pos;
			pkt->convergence_duration = pkt1->pkt.convergence_duration;
			//*pkt = pkt1->pkt;
			
			if((pkt->data != NULL) && (pkt1->pkt.data != NULL))
				memcpy(pkt->data, pkt1->pkt.data, pkt1->pkt.size);

			//HINT SEE BEFORE q->size -= pkt1->pkt.size;
			q->first_pkt = RemoveFromQueue(q, pkt1);

			ret = 1;
			UpdateQueueStats(q, pkt->stream_index);
			//update index of last frame extracted
			q->last_frame_extracted = pkt->stream_index;
		}
#ifdef DEBUG_QUEUE
		else {
			printf("  VIDEO pk1 NULL!!!!\n");
		}
#endif
	}

	if(q->nb_packets==0 && q->queueType==AUDIO) {
		QueueFillingMode=1;
#ifdef DEBUG_QUEUE
		printf("QUEUE: Get FillingMode ON\n");
#endif
	}
#ifdef DEBUG_QUEUE
	printf("QUEUE: Get Last %s Frame Extracted = %d\n", (q->queueType==AUDIO) ? "AUDIO" : "VIDEO", q->last_frame_extracted);
#endif

	SDL_UnlockMutex(q->mutex);
	return ret;
}

int AudioDecodeFrame(uint8_t *audio_buf, int buf_size) {
	//struct timeval now;
	int audio_pkt_size = 0;
	long long Now;
	short int DecodeAudio=0, SkipAudio=0;
	//int len1, data_size;

	//gettimeofday(&now,NULL);
	//Now = (now.tv_sec)*1000+now.tv_usec/1000;
	Now=(long long)SDL_GetTicks();

	if(QueueFillingMode || QueueStopped)
	{
		//SDL_LockMutex(timing_mutex);
		FirstTimeAudio=1;
		FirstTime = 1;
		//SDL_UnlockMutex(timing_mutex);
		return -1;
	}

	if((FirstTime==1 || FirstTimeAudio==1) && audioq.size>0) {
		if(audioq.first_pkt->pkt.pts>0)
		{
			//SDL_LockMutex(timing_mutex);
			DeltaTime=Now-(long long)(audioq.first_pkt->pkt.pts);
			FirstTimeAudio = 0;
			FirstTime = 0;
			//SDL_UnlockMutex(timing_mutex);
#ifdef DEBUG_AUDIO 
		 	printf("AUDIO: audio_decode_frame - DeltaTimeAudio=%lld\n",DeltaTime);
#endif
		}
	}

#ifdef DEBUG_AUDIO 
	if(audioq.first_pkt)
	{
		printf("AUDIO: audio_decode_frame - Syncro params: Delta:%lld Now:%lld pts=%lld pts+Delta=%lld ",(long long)DeltaTime,Now,(long long)audioq.first_pkt->pkt.pts,(long long)audioq.first_pkt->pkt.pts+DeltaTime);
		printf("AUDIO: QueueLen=%d ",(int)audioq.nb_packets);
		printf("AUDIO: QueueSize=%d\n",(int)audioq.size);
	}
	else
		printf("AUDIO: audio_decode_frame - Empty queue\n");
#endif


	if(audioq.nb_packets>0) {
		if((long long)audioq.first_pkt->pkt.pts+DeltaTime<Now-(long long)MAX_TOLLERANCE) {
			SkipAudio = 1;
			DecodeAudio = 0;
		}
		else if((long long)audioq.first_pkt->pkt.pts+DeltaTime>=Now-(long long)MAX_TOLLERANCE &&
			(long long)audioq.first_pkt->pkt.pts+DeltaTime<=Now+(long long)MAX_TOLLERANCE) {
				SkipAudio = 0;
				DecodeAudio = 1;
		}
	}
		
	while(SkipAudio==1 && audioq.size>0) {
		audioq.total_skips++;
		SkipAudio = 0;
#ifdef DEBUG_AUDIO
 		printf("AUDIO: skipaudio: queue size=%d\n",audioq.size);
#endif
		if(PacketQueueGet(&audioq,&AudioPkt,1) < 0) {
			return -1;
		}
		if(audioq.first_pkt)
		{
			if((long long)audioq.first_pkt->pkt.pts+DeltaTime<Now-(long long)MAX_TOLLERANCE) {
				SkipAudio = 1;
				DecodeAudio = 0;
			}
			else if((long long)audioq.first_pkt->pkt.pts+DeltaTime>=Now-(long long)MAX_TOLLERANCE &&
				(long long)audioq.first_pkt->pkt.pts+DeltaTime<=Now+(long long)MAX_TOLLERANCE) {
					SkipAudio = 0;
					DecodeAudio = 1;
			}
		}
	}
	if(DecodeAudio==1) {
		if(PacketQueueGet(&audioq,&AudioPkt,1) < 0) {
			return -1;
		}
		memcpy(audio_buf,AudioPkt.data,AudioPkt.size);
		audio_pkt_size = AudioPkt.size;
#ifdef DEBUG_AUDIO
 		printf("AUDIO: Decode audio\n");
#endif
	}

	return audio_pkt_size;
}

int VideoCallback(void *valthread)
{
	//AVPacket pktvideo;
	AVCodecContext  *pCodecCtx;
	AVCodec         *pCodec;
	AVFrame         *pFrame;
	int frameFinished;
	AVPicture pict;
	long long Now;
	short int SkipVideo, DecodeVideo;

	//double frame_rate = 0.0,time_between_frames=0.0;
	//struct timeval now;

	//int wait_for_sync = 1;
	ThreadVal *tval;
	tval = (ThreadVal *)valthread;

	//frame_rate = tval->framerate;
	//time_between_frames = 1.e6 / frame_rate;
	//gettimeofday(&time_now,0);

	//frecon = fopen("recondechunk.mpg","wb");

	pCodecCtx=avcodec_alloc_context();
	pCodecCtx->codec_type = CODEC_TYPE_VIDEO;
#ifdef H264_VIDEO_ENCODER
	pCodecCtx->codec_id  = CODEC_ID_H264;
	pCodecCtx->me_range = 16;
	pCodecCtx->max_qdiff = 4;
	pCodecCtx->qmin = 1;
	pCodecCtx->qmax = 30;
	pCodecCtx->qcompress = 0.6;
#else
	pCodecCtx->codec_id  = CODEC_ID_MPEG4;
#endif
	//pCodecCtx->bit_rate = 400000;
	// resolution must be a multiple of two
	pCodecCtx->width = tval->width;//176;//352;
	pCodecCtx->height = tval->height;//144;//288;
	
	// frames per second
	//pCodecCtx->time_base = (AVRational){1,25};
	//pCodecCtx->gop_size = 10; // emit one intra frame every ten frames
	//pCodecCtx->max_b_frames=1;
	pCodecCtx->pix_fmt = PIX_FMT_YUV420P;
	pCodec=avcodec_find_decoder(pCodecCtx->codec_id);

	if(pCodec==NULL) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found
	}
	if(avcodec_open(pCodecCtx, pCodec) < 0) {
		fprintf(stderr, "could not open codec\n");
		return -1; // Could not open codec
	}
	pFrame=avcodec_alloc_frame();
	if(pFrame==NULL) {
		printf("Memory error!!!\n");
		return -1;
	}
	
#ifdef DEBUG_VIDEO
 	printf("VIDEO: video_callback entering main cycle\n");
#endif
	while(AVPlaying && !quit) {
		if(QueueFillingMode || QueueStopped)
		{
			//SDL_LockMutex(timing_mutex);
			FirstTime = 1;
			//SDL_UnlockMutex(timing_mutex);
			usleep(5000);
			continue;
		}

		DecodeVideo = 0;
		SkipVideo = 0;
		Now=(long long)SDL_GetTicks();
		if(FirstTime==1 && videoq.size>0) {
			if(videoq.first_pkt->pkt.pts>0)
			{
				//SDL_LockMutex(timing_mutex);
				DeltaTime=Now-(long long)videoq.first_pkt->pkt.pts;
				FirstTime = 0;
				//SDL_UnlockMutex(timing_mutex);
			}
#ifdef DEBUG_VIDEO 
		 	printf("VIDEO: VideoCallback - DeltaTimeAudio=%lld\n",DeltaTime);
#endif
		}

#ifdef DEBUG_VIDEO 
		if(videoq.first_pkt)
		{
			printf("VIDEO: VideoCallback - Syncro params: Delta:%lld Now:%lld pts=%lld pts+Delta=%lld ",(long long)DeltaTime,Now,(long long)videoq.first_pkt->pkt.pts,(long long)videoq.first_pkt->pkt.pts+DeltaTime);
			printf("VIDEO: Index=%d ", (int)videoq.first_pkt->pkt.stream_index);
			printf("VIDEO: QueueLen=%d ", (int)videoq.nb_packets);
			printf("VIDEO: QueueSize=%d\n", (int)videoq.size);
		}
		else
			printf("VIDEO: VideoCallback - Empty queue\n");
#endif

		if(videoq.nb_packets>0) {
			if(((long long)videoq.first_pkt->pkt.pts+DeltaTime)<Now-(long long)MAX_TOLLERANCE) {
				SkipVideo = 1;
				DecodeVideo = 0;
			}
			else 
				if(((long long)videoq.first_pkt->pkt.pts+DeltaTime)>=Now-(long long)MAX_TOLLERANCE &&
				   ((long long)videoq.first_pkt->pkt.pts+DeltaTime)<=Now+(long long)MAX_TOLLERANCE) {
					SkipVideo = 0;
					DecodeVideo = 1;
				}
		}
#ifdef DEBUG_VIDEO
		printf("VIDEO: skipvideo:%d decodevideo:%d\n",SkipVideo,DecodeVideo);
#endif

		while(SkipVideo==1 && videoq.size>0) {
			videoq.total_skips++;
			SkipVideo = 0;
#ifdef DEBUG_VIDEO 
 			printf("VIDEO: Skip Video\n");
#endif
			if(PacketQueueGet(&videoq,&VideoPkt,0) < 0) {
				break;
			}
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &VideoPkt);
			if(videoq.first_pkt)
			{
				if((long long)videoq.first_pkt->pkt.pts+DeltaTime<Now-(long long)MAX_TOLLERANCE) {
					SkipVideo = 1;
					DecodeVideo = 0;
				}
				else if((long long)videoq.first_pkt->pkt.pts+DeltaTime>=Now-(long long)MAX_TOLLERANCE &&
								(long long)videoq.first_pkt->pkt.pts+DeltaTime<=Now+(long long)MAX_TOLLERANCE) {
					SkipVideo = 0;
					DecodeVideo = 1;
				}
			}
		}
		
		if(DecodeVideo==1) {
			if(PacketQueueGet(&videoq,&VideoPkt,0) > 0) {

#ifdef DEBUG_VIDEO
				printf("VIDEO: Decode video FrameTime=%lld Now=%lld\n",(long long)VideoPkt.pts+DeltaTime,Now);
#endif

				avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &VideoPkt);

				if(frameFinished) { // it must be true all the time else error
#ifdef DEBUG_VIDEO
					printf("VIDEO: FrameFinished\n");
#endif
					if(SaveYUV)
						SaveFrame(pFrame, pCodecCtx->width, pCodecCtx->height);
					//fwrite(pktvideo.data, 1, pktvideo.size, frecon);

					if(SilentMode)
						continue;

					// Lock SDL_yuv_overlay
					if(SDL_MUSTLOCK(MainScreen)) {
						if(SDL_LockSurface(MainScreen) < 0) {
							continue;
						}
					}

					if(SDL_LockYUVOverlay(YUVOverlay) < 0) {
						if(SDL_MUSTLOCK(MainScreen)) {
							SDL_UnlockSurface(MainScreen);
						}
						continue;
					}
					
					pict.data[0] = YUVOverlay->pixels[0];
					pict.data[1] = YUVOverlay->pixels[2];
					pict.data[2] = YUVOverlay->pixels[1];

					pict.linesize[0] = YUVOverlay->pitches[0];
					pict.linesize[1] = YUVOverlay->pitches[2];
					pict.linesize[2] = YUVOverlay->pitches[1];

					if(img_convert_ctx == NULL) {
						img_convert_ctx = sws_getContext(tval->width, tval->height, PIX_FMT_YUV420P, InitRect->w, InitRect->h, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
						if(img_convert_ctx == NULL) {
							fprintf(stderr, "Cannot initialize the conversion context!\n");
							exit(1);
						}
					}
					// let's draw the data (*yuv[3]) on a SDL screen (*screen)
					sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, tval->height, pict.data, pict.linesize);
					SDL_UnlockYUVOverlay(YUVOverlay);
					// Show, baby, show!
					SDL_LockMutex(OverlayMutex);
					SDL_DisplayYUVOverlay(YUVOverlay, &OverlayRect);
					SDL_UnlockMutex(OverlayMutex);

					//redisplay logo
					/**SDL_BlitSurface(image, NULL, MainScreen, &dest);*/
					/* Update the screen area just changed */
					/**SDL_UpdateRects(MainScreen, 1, &dest);*/

					if(SDL_MUSTLOCK(MainScreen)) {
						SDL_UnlockSurface(MainScreen);
					}
				} //if FrameFinished
			} // if packet_queue_get
		} //if DecodeVideo=1

		usleep(5000);
	}
	
	av_free(pCodecCtx);
	av_free(pFrame);
	//fclose(frecon);
#ifdef DEBUG_VIDEO
 	printf("VIDEO: video callback end\n");
#endif
	return 0;
}

void AudioCallback(void *userdata, Uint8 *stream, int len)
{
	//AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int audio_size;

	static uint8_t audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];

	audio_size = AudioDecodeFrame(audio_buf, sizeof(audio_buf));
	
	if(!SilentMode)
		if(audio_size != len) {
			memset(stream, 0, len);
		} else {
			memcpy(stream, (uint8_t *)audio_buf, len);
		}
}

void SaveFrame(AVFrame *pFrame, int width, int height)
{
	FILE *pFile;
	int  y;
  
	 // Open file
	pFile=fopen(YUVFileName, "ab");
	if(pFile==NULL)
		return;
  
	// Write header
	//fprintf(pFile, "P5\n%d %d\n255\n", width, height);
  
	// Write Y data
	for(y=0; y<height; y++)
  		fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width, pFile);
	// Write U data
	for(y=0; y<height/2; y++)
  		fwrite(pFrame->data[1]+y*pFrame->linesize[1], 1, width/2, pFile);
	// Write V data
	for(y=0; y<height/2; y++)
  		fwrite(pFrame->data[2]+y*pFrame->linesize[2], 1, width/2, pFile);
  
	// Close file
	fclose(pFile);
}

int ChunkerPlayerCore_IsRunning()
{
	return AVPlaying;
}

void ChunkerPlayerCore_Play()
{
	if(AVPlaying) return;
	
	AVPlaying = 1;
	SDL_PauseAudio(0);
	video_thread = SDL_CreateThread(VideoCallback, &VideoCallbackThreadParams);
}

void ChunkerPlayerCore_Stop()
{
	if(!AVPlaying) return;
	
	AVPlaying = 0;
	
	// Stop audio&video playback
	SDL_WaitThread(video_thread, NULL);
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	
	if(YUVOverlay != NULL)
	{
		SDL_FreeYUVOverlay(YUVOverlay);
		YUVOverlay = NULL;
	}
	
	PacketQueueReset(&audioq);
	PacketQueueReset(&videoq);
	
	av_free(aCodecCtx);
	free(AudioPkt.data);
	free(VideoPkt.data);
	free(outbuf_audio);
	free(InitRect);
}

int ChunkerPlayerCore_AudioEnded()
{
	return (audioq.nb_packets==0 && audioq.last_frame_extracted>0);
}

void ChunkerPlayerCore_ResetAVQueues()
{
#ifdef DEBUG_QUEUE
	printf("QUEUE: MAIN SHOULD RESET\n");
#endif
	PacketQueueReset(&audioq);
	PacketQueueReset(&videoq);
}

int ChunkerPlayerCore_EnqueueBlocks(const uint8_t *block, const int block_size)
{
	Chunk *gchunk = NULL;
	int decoded_size = -1;
	uint8_t *tempdata, *buffer;
	int j;
	Frame *frame = NULL;
	AVPacket packet, packetaudio;

	uint16_t *audio_bufQ = NULL;

	//the frame.h gets encoded into 5 slots of 32bits (3 ints plus 2 more for the timeval struct
	static int sizeFrameHeader = 5*sizeof(int32_t);
	static int ExternalChunk_header_size = 5*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 2*CHUNK_TRANSCODING_INT_SIZE + 1*CHUNK_TRANSCODING_INT_SIZE*2;

	static int chunks_out_of_order = 0;
	static int last_chunk_id = -1;

	audio_bufQ = (uint16_t *)av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
	if(!audio_bufQ) {
		printf("Memory error in audio_bufQ!\n");
		return PLAYER_FAIL_RETURN;
	}

	gchunk = (Chunk *)malloc(sizeof(Chunk));
	if(!gchunk) {
		printf("Memory error in gchunk!\n");
		av_free(audio_bufQ);
		return PLAYER_FAIL_RETURN;
	}

	decoded_size = decodeChunk(gchunk, block, block_size);

	if(last_chunk_id == -1)
		last_chunk_id = gchunk->id;

	if(gchunk->id > (last_chunk_id+1)) {
		chunks_out_of_order += gchunk->id - last_chunk_id - 1;
	}
	last_chunk_id = gchunk->id;

#ifdef DEBUG_CHUNKER
	printf("CHUNKER: enqueueBlock: id %d decoded_size %d target size %d - skipped %d\n", gchunk->id, decoded_size, GRAPES_ENCODED_CHUNK_HEADER_SIZE + ExternalChunk_header_size + gchunk->size, chunks_out_of_order);
#endif
  if(decoded_size < 0) {
		//HINT here i should differentiate between various return values of the decode
		//in order to free what has been allocated there
		printf("chunk probably corrupted!\n");
		av_free(audio_bufQ);
		free(gchunk);
		return PLAYER_FAIL_RETURN;
	}

	frame = (Frame *)malloc(sizeof(Frame));
	if(!frame) {
		printf("Memory error in Frame!\n");
		if(gchunk) {
			if(gchunk->attributes) {
				free(gchunk->attributes);
			}
			free(gchunk);
		}
		av_free(audio_bufQ);
		return PLAYER_FAIL_RETURN;
	}

	tempdata = gchunk->data; //let it point to first frame of payload
	j=gchunk->size;
	while(j>0 && !quit) {
		frame->number = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->timestamp.tv_sec = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->timestamp.tv_usec = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->size = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;
		frame->type = bit32_encoded_pull(tempdata);
		tempdata += CHUNK_TRANSCODING_INT_SIZE;

		buffer = tempdata; // here coded frame information
		tempdata += frame->size; //let it point to the next frame

		if(frame->type < 5) { // video frame
			av_init_packet(&packet);
			packet.data = buffer;//video_bufQ;
			packet.size = frame->size;
			packet.pts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			packet.dts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			packet.stream_index = frame->number; // use of stream_index for number frame
			//packet.duration = frame->timestamp.tv_sec;
			if(packet.size > 0)
				ChunkerPlayerCore_PacketQueuePut(&videoq, &packet); //the _put makes a copy of the packet

#ifdef DEBUG_SOURCE
			printf("SOURCE: Insert video in queue pts=%lld %d %d sindex:%d\n",packet.pts,(int)frame->timestamp.tv_sec,(int)frame->timestamp.tv_usec,packet.stream_index);
#endif
		}
		else if(frame->type == 5) { // audio frame
			av_init_packet(&packetaudio);
			packetaudio.data = buffer;
			packetaudio.size = frame->size;
			packetaudio.pts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			packetaudio.dts = frame->timestamp.tv_sec*(unsigned long long)1000+frame->timestamp.tv_usec;
			//packetaudio.duration = frame->timestamp.tv_sec;
			packetaudio.stream_index = frame->number; // use of stream_index for number frame
			packetaudio.flags = 1;
			packetaudio.pos = -1;

			//instead of -1, in order to signal it is not decoded yet
			packetaudio.convergence_duration = 0;

			// insert the audio frame into the queue
			if(packetaudio.size > 0)
				ChunkerPlayerCore_PacketQueuePut(&audioq, &packetaudio);//makes a copy of the packet so i can free here

#ifdef DEBUG_SOURCE
			printf("SOURCE: Insert audio in queue pts=%lld sindex:%d\n", packetaudio.pts, packetaudio.stream_index);
#endif
		}
		else {
			printf("SOURCE: Unknown frame type %d. Size %d\n", frame->type, frame->size);
		}
		if(frame->size > 0)
			j = j - sizeFrameHeader - frame->size;
		else {
			printf("SOURCE: Corrupt frames (size %d) in chunk. Skipping it...\n", frame->size);
			j = -1;
		}
	}
	//chunk ingestion terminated!
	if(gchunk) {
		if(gchunk->attributes) {
			free(gchunk->attributes);
		}
		free(gchunk);
	}
	if(frame)
		free(frame);
	if(audio_bufQ)
		av_free(audio_bufQ);
		
	return PLAYER_OK_RETURN;
}

void ChunkerPlayerCore_SetupOverlay(int width, int height)
{
	// if(!MainScreen && !SilentMode)
	// {
		// printf("Cannot find main screen, exiting...\n");
		// exit(1);
	// }
	
	if(SilentMode)
		return;
		
	SDL_LockMutex(OverlayMutex);
	if(YUVOverlay != NULL)
	{
		SDL_FreeYUVOverlay(YUVOverlay);
		YUVOverlay = NULL;
	}
	
	// create video overlay for display of video frames
	// printf("SDL_CreateYUVOverlay(%d, %d, SDL_YV12_OVERLAY, MainScreen)\n", width, height);
	YUVOverlay = SDL_CreateYUVOverlay(width, height, SDL_YV12_OVERLAY, MainScreen);
	// YUVOverlay = SDL_CreateYUVOverlay(OverlayRect.w, OverlayRect.h, SDL_YV12_OVERLAY, MainScreen);
	if ( YUVOverlay == NULL )
	{
		fprintf(stderr,"SDL: Couldn't create SDL_yuv_overlay: %s", SDL_GetError());
		exit(1);
	}

	if ( YUVOverlay->hw_overlay )
		fprintf(stderr,"SDL: Using hardware overlay.\n");
	// OverlayRect.x = (screen_w - width) / 2;
	
	SDL_DisplayYUVOverlay(YUVOverlay, &OverlayRect);
	
	SDL_UnlockMutex(OverlayMutex);
}
