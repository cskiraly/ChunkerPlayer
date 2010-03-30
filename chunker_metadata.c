// chunkbuffer.c
// Author 
// Giuseppe Tropea
//
// Use the file compile to compile the program to build (assuming libavformat and libavcodec are 
// correctly installed your system).
//
// Run using
//
// ingestion myvideofile.mpg


#include "chunker_metadata.h"


void chunker_trim(char *s) {
	// Trim spaces and tabs from beginning:
	int i=0,j;
	while((s[i]==' ')||(s[i]=='\t')) {
		i++;
	}
	if(i>0) {
		for(j=0;j<strlen(s);j++) {
			s[j]=s[j+i];
		}
		s[j]='\0';
	}

	// Trim spaces and tabs from end:
	i=strlen(s)-1;
	while((s[i]==' ')||(s[i]=='\t') || s[i]=='\n') {
		i--;
	}
	if(i<(strlen(s)-1)) {
		s[i+1]='\0';
	}
}

/* Read config file for chunk strategy [numframes:num|size:num] and create a new chunk_buffer object
	numframes:num	fill each chunk with the same number of frame
	size:num	fill each chunk with the size of bytes no bigger than num
*/
struct chunker_metadata *chunkerInit(const char *config) {
	int isstrategy;
	char str[1000];
	char *p;
	fprintf(stderr,"Calling chunkerInit...\n");
	FILE *fp = fopen(config,"r");
	ChunkerMetadata *cmeta = (ChunkerMetadata *)malloc(sizeof(ChunkerMetadata));
	cmeta->echunk = NULL;
	cmeta->size = 0;
	cmeta->val_strategy = 0;
	while(fgets(str,100,fp)!=NULL) {
		chunker_trim(str);
		if(str[0]=='#')
			continue;
		if(!strcmp(str,"[strategy]")) {
			isstrategy=1;
			continue;
		}
		if(isstrategy==1) {
			p = strtok(str,":");
			chunker_trim(p);
			if(!(strcmp(p,"numframes")))
				cmeta->strategy=0;
			if(!(strcmp(p,"size")))
				cmeta->strategy=1;
			p = strtok(NULL,":");
			sscanf(p,"%d",&cmeta->val_strategy);
			isstrategy=0;
		}
	}
	fclose(fp);
	fprintf(stderr,"done!\n");
	return cmeta;
}

