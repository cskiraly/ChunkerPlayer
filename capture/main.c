#include "capture.h"
#include "video.h"
#include "audio.h"

void process_frame (unsigned char *p, int bytes, const struct v4l2_format* fmt)
{
    switch(fmt->fmt.pix.pixelformat)
    {
        case V4L2_PIX_FMT_JPEG:
            sprintf(filename, "frames/jpg/frame%d.jpg", seq++);
            img = fopen(filename,"w");
            fwrite(p, bytes, 1, img);
            fclose(img);
            break;
        case V4L2_PIX_FMT_YUYV:
            sprintf(filename, "frames/ppm/frame%d.ppm", seq++);
            img = fopen(filename,"w");

            // RGB
            //fprintf( img, "P6\r\n" );

            // GRAY
            fprintf( img, "P5\r\n" );

            fprintf( img, "# comment .\r\n" );
            fprintf( img, "%d %d\r\n", fmt->fmt.pix.width, fmt->fmt.pix.height );
            fprintf( img, "255\r\n" );

            unsigned int pixels = fmt->fmt.pix.width*fmt->fmt.pix.height;

            // YUYV 2 GRAY            
            int i;
            for(i=0; i<bytes; i+=2)
                fwrite(p+i, 1, 1, img);

            // YUYV 2 RGB24
            // todo

            fclose(img);
            break;
        case V4L2_PIX_FMT_MJPEG:
            sprintf(filename, "frames/jpg/frame%d.jpg", seq++);
            img = fopen(filename,"w");
            fwrite(p, bytes, 1, img);
            fclose(img);
            break;
    }
    
}

void process_sample(unsigned char* buffer, int buffer_size)
{
    fwrite(buffer, buffer_size, 1, audio_output_file);
}

void usage (FILE *fp, int argc, char **argv)
{
    fprintf (fp,
        "Usage: %s [options]\n\n"
        "Options:\n"
        "-v | --video-device    Video device name default=/dev/video0\n"
        "-a | --audio-device    Audio device name [hw:0,1|hw:0,2|...], default=default\n"
        "-h | --help            Print this message\n"
        //"-m | --mmap            Use memory mapped buffers\n"
        //"-r | --read            Use read() calls\n"
        //"-u | --userp           Use application allocated buffers\n"
        "--video-format         Video format [rgb24|yuyv|jpeg|mjpeg], default=yuyv\n\n"
        //"--audio-format         Audio format []\n\n"
        "",
        argv[0]);
}

int main (int argc, char **argv)
{
    // VIDEO
    seq = 0;
    video_dev_name = DEFAULT_VIDEO_DEVICE;
    io = IO_METHOD_MMAP;
    video_format = DEFAULT_VIDEO_FORMAT;
    fd = -1;
    buffers = NULL;
    n_buffers = 0;
    system("rm frames/ppm/* -f");

    // AUDIO
    char* audio_dev_name = "default";
    audio_output_file = fopen(DEFAULT_AUDIO_OUT_FILE, "w");

    for (;;)
    {
        int index;
        int c;
        const struct option long_options [] = {
            { "video-device",   required_argument,      NULL,           'v' },
            { "audio-device",   required_argument,      NULL,           'a' },
            { "help",           no_argument,            NULL,           'h' },
            { "mmap",           no_argument,            NULL,           'm' },
            { "read",           no_argument,            NULL,           'r' },
            { "userp",          no_argument,            NULL,           'u' },
            { "video-format",   required_argument,      NULL,           'x' },
            //{ "audio-format",   required_argument,      NULL,           'z' },
            { 0, 0, 0, 0 }
        };

        c = getopt_long (argc, argv, "v:a:hmrux:", long_options, &index);

        if (-1 == c)
            break;

        switch (c)
        {
            case 0: /* getopt_long() flag */
                break;

            case 'v':
                video_dev_name = optarg;
                break;

            case 'h':
                usage (stdout, argc, argv);
                exit (EXIT_SUCCESS);

            case 'x':                
                if(strcmp(optarg, "rgb24") == 0)
                    video_format = V4L2_PIX_FMT_RGB24;
                if(strcmp(optarg, "jpeg") == 0)
                    video_format = V4L2_PIX_FMT_JPEG;
                if(strcmp(optarg, "yuyv") == 0)
                    video_format = V4L2_PIX_FMT_YUYV;
                if(strcmp(optarg, "yuv32") == 0)
                    video_format = V4L2_PIX_FMT_YUV32;
                if(strcmp(optarg, "grey") == 0)
                    video_format = V4L2_PIX_FMT_GREY;
                if(strcmp(optarg, "mjpeg") == 0)
                    video_format = V4L2_PIX_FMT_MJPEG;
                break;

            case 'a':
                audio_dev_name = optarg;
                break;

            case 'm':
                io = IO_METHOD_MMAP;
                break;

            case 'r':
                io = IO_METHOD_READ;
                break;

            case 'u':
                io = IO_METHOD_USERPTR;
                break;

            default:
                usage (stderr, argc, argv);
                exit (EXIT_FAILURE);
        }
    }

    // VIDEO INIT
    //printf("opening video device...\n");
    open_video_device ();
    //printf("\t success\n\n");
    printf("initializing video device...\n");
    init_video_device ();
    start_video_capturing();
    printf("OK\n\n");

    // AUDIO INIT
    printf("initializing audio device...\n");
    int error = init_audio(audio_dev_name);
    if(error<0)
        exit(1);
    printf("OK\n\n");

    signal(SIGINT, terminate);

    running = 1;
    pthread_create(&video_capure_thread, NULL, video_capture, NULL);
    pthread_create(&audio_capure_thread, NULL, audio_capture, NULL);

    pthread_join(video_capure_thread, NULL);
    pthread_join(audio_capure_thread, NULL);

    printf("stopping video capture...\n");
    stop_video_capturing ();
    uninit_video_device ();
    close_video_device ();
    printf("STOPPED\n\n");

    printf("stopping audio capture...\n");
    fclose(audio_output_file);
    close_audio_device();
    printf("STOPPED\n\n");

    exit (EXIT_SUCCESS);

    return 0;
}

void terminate(int signum)
{
    printf("\nterminating...\n");
    running = 0;
}

void* video_capture(void* ThreadParams)
{
    printf("video capture started\n");
    while(running)
    {
        int r = poll_video_device();

        if (-1 == r) {
            if (EINTR == errno)
                continue;

            errno_exit ("select");
        }

        if (0 == r) {
            fprintf (stderr, "select timeout\n");
            exit (EXIT_FAILURE);
        }
        read_frame();
    }
}

void* audio_capture(void* ThreadParams)
{
    printf("audio capture started (%d bit)\n", audio_s_bit);
    wav_t head = {
            {'R','I','F','F'}
            ,(1<<28)+36,
            {'W','A','V','E'},
            {'f','m','t',' '},
            audio_s_bit,1,AUDIO_CHANNELS, AUDIO_SAMPLE_RATE,4*AUDIO_SAMPLE_RATE,4,audio_s_bit,
            {'d','a','t','a'},
            1<<30
    };

    fwrite(&head,sizeof(wav_t),1, audio_output_file);
    while(running)
    {
        read_sample();
    }
}
