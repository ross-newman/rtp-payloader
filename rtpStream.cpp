/*
 * Might need to add a route here:
 * 	sudo route add -net 239.0.0.0 netmask 255.0.0.0 eth1
 */

#include <pthread.h>
#include <sched.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include "rtpStream.h"

#define RTP_TO_YUV_ONGPU 	0 // Offload colour conversion to GPU if set
#define RTP_CHECK 			  0 // 0 to disable RTP header checking
#define RTP_THREADED 		  0 // transmit and recieve in a thread. RX thread blocks TX does not
#define PITCH 				    4 // RGBX processing pitch

void DumpHex(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

#if ENDIAN_SWAP
	void endianswap32(uint32_t *data, int length);
	void endianswap16(uint16_t *data, int length);
#endif

typedef struct float4 {
    float x;
    float y;
    float z;
    float w;
} float4;

/*
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

void rgbtoyuv(int y, int x, char* yuv, char* rgb)
{
  int c,cc,R,G,B,Y,U,V;
  int size;

  cc=0;
  size = x*PITCH;
  for (c=0;c<size;c+=PITCH)
  {
    R=rgb[c];
    G=rgb[c+1];
    B=rgb[c+2];
    /* sample luma for every pixel */
    Y  = (0.257 * R) + (0.504 * G) + (0.098 * B) + 16;

    yuv[cc+1]=Y;
    if (c % 2 != 0)

    {
        V =  (0.439 * R) - (0.368 * G) - (0.071 * B) + 128;
        yuv[cc]=V;
    }
    else
    {
        U = -(0.148 * R) - (0.291 * G) + (0.439 * B) + 128;
        yuv[cc]=U;
    }
    cc+=2;
  }
}

rtpStream::rtpStream(int height, int width)
{
	mHeight = height;
	mWidth = width;
    mFrame = 0;
    mPortNoIn = 0;
    mPortNoOut = 0;
    gpuBuffer = 0;
    pthread_mutex_init(&mutex, NULL);
    bufferIn = (char*)malloc(height * width * 2); // Holds YUV data
	printf("[RTP] rtpStream created %dx%d\n", mWidth, mHeight);
}

rtpStream::~rtpStream(void)
{
	free(bufferIn);
}

/* Broadcast the stream to port i.e. 5004 */
void rtpStream::rtpStreamIn( char* hostname, int portno)
{
	printf("[RTP] rtpStreamIn %s %d\n", hostname, portno);
	mPortNoIn = portno;
	strcpy(mHostnameIn, hostname);
}

void rtpStream::rtpStreamOut(char* hostname, int portno)
{
	printf("[RTP] rtpStreamOut %s %d\n", hostname, portno);
	mPortNoOut = portno;
	strcpy(mHostnameOut, hostname);
}

bool rtpStream::Open()
{
	if (mPortNoIn)
	{
		struct sockaddr_in si_me;

		int i, slen = sizeof(si_me);

		//create a UDP socket
		if ((mSockfdIn=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		{
			printf("ERROR opening socket\n");
			return error;
		}

		// zero out the structure
		memset((char *) &si_me, 0, sizeof(si_me));

		si_me.sin_family = AF_INET;
		si_me.sin_port = htons(mPortNoIn);
		si_me.sin_addr.s_addr = htonl(INADDR_ANY);
		
		//bind socket to port
		if( bind(mSockfdIn , (struct sockaddr*)&si_me, sizeof(si_me) ) == -1)
		{
			printf("ERROR binding socket\n");
			return error;
		}
#if	RTP_MULTICAST
		{
			struct ip_mreq multi;
			
			// Multicast
			multi.imr_multiaddr.s_addr = inet_addr(IP_MULTICAST_IN);
			multi.imr_interface.s_addr = htonl(INADDR_ANY);
			if (setsockopt(mSockfdIn, IPPROTO_UDP, IP_ADD_MEMBERSHIP, &multi, sizeof(multi)) < 0)
			{
				printf("ERROR failed to join multicast group %s\n", IP_MULTICAST_IN);			
			}
		}
#endif
	}
 
	if (mPortNoOut)
	{
		/* socket: create the outbound socket */
		mSockfdOut = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (mSockfdOut < 0)
		{
			printf("ERROR opening socket\n");
			return error;
		}

		/* gethostbyname: get the server's DNS entry */
		mServerOut = gethostbyname(mHostnameOut);
		if (mServerOut == NULL) {
			fprintf(stderr,"ERROR, no such host as %s\n", mHostnameOut);
			exit(0);
		}

		/* build the server's Internet address */
		bzero((char *) &mServeraddrOut, sizeof(mServeraddrOut));
		mServeraddrOut.sin_family = AF_INET;
		bcopy((char *)mServerOut->h_addr,
		  (char *)&mServeraddrOut.sin_addr.s_addr, mServerOut->h_length);
		mServeraddrOut.sin_port = htons(mPortNoOut);

		/* send the message to the server */
		mServerlenOut = sizeof(mServeraddrOut);
#if 0		
		int n = sendto(mSockfdOut, (char *)"hello", 5, 0, (const sockaddr*)&mServeraddrOut, mServerlenOut);
		printf("n=%d\n", n);
		if (n < 0 ) 
		{
			printf("[RTP] Transmit socket failure fd=%d\n", mSockfdOut);
			return n;
		}
#endif	
	}

    return true;
}

void rtpStream::Close()
{
	if (mPortNoIn)
	{
		close(mSockfdIn);
	}

	if (mPortNoOut)
	{
		close(mSockfdOut);
	}
}

#if ENDIAN_SWAP
void endianswap32(uint32_t *data, int length)
{
	int c = 0;
	for (c=0;c<length;c++)
		data[c] = __bswap_32 (data[c]);
}

void endianswap16(uint16_t *data, int length)
{
	int c = 0;
	for (c=0;c<length;c++)
		data[c] = __bswap_16 (data[c]);
}
#endif

void rtpStream::update_header(header *packet, int line, int last, int32_t timestamp, int32_t source)
{
	bzero((char *)packet, sizeof(header));
	packet->rtp.protocol = RTP_VERSION << 30;
	packet->rtp.protocol = packet->rtp.protocol | RTP_PAYLOAD_TYPE << 16;
	packet->rtp.protocol = packet->rtp.protocol | sequence_number++;
	/* leaving other fields as zero TODO Fix*/
	packet->rtp.timestamp = timestamp += (Hz90 / RTP_FRAMERATE);
	packet->rtp.source = source;
	packet->payload.extended_sequence_number = 0; /* TODO : Fix extended seq numbers */
	packet->payload.line[0].length = mWidth*2;
	packet->payload.line[0].line_number = line;
	packet->payload.line[0].offset = 0;
	if (last==1)
	{
		packet->rtp.protocol = packet->rtp.protocol | 1 << 23;
	}
  
  printf("packet->payload 0x%x\n", packet->rtp.protocol);
  printf("packet->timestamp 0x%x\n", packet->rtp.timestamp);
  printf("packet->source 0x%x\n", packet->rtp.source);
  printf(">>> Dump\n");
  DumpHex(packet, 32);
}

void *ReceiveThread(void* data)
{
	tx_data *arg;
	ssize_t len=0;
	rtp_packet *packet;
	bool receiving = true;
	int scancount = 0;
	int lastpacket;

	arg = (tx_data *)data;

	while (receiving)
	{
		int marker;
#if RTP_CHECK
		int version;
		int payloadType;
		int seqNo, last;
#endif
		bool valid = false;

		//
		// Read data until we get the next RTP header
		//
		while (!valid)
		{
			//
			// Read in the RTP data
			// 
			len = recvfrom(arg->stream->mSockfdIn, arg->stream->udpdata, MAX_UDP_DATA, 0, NULL, NULL);

			packet = (rtp_packet *)arg->stream->udpdata;
#if ENDIAN_SWAP
			endianswap32((uint32_t *)packet, sizeof(rtp_header)/4);
#endif
			//
			// Decode Header bits and confirm RTP packet
			//
#if RTP_CHECK
			payloadType = (packet->head.rtp.protocol & 0x007F0000) >> 16;
			version = (packet->head.rtp.protocol & 0xC0000000) >> 30;
			seqNo = (packet->head.rtp.protocol & 0x0000FFFF);
			if ((payloadType == 96) && (version ==2))
#endif
			{
#if 0
				if (seqNo != last + 1) {

					printf("Dropped %d packets (%d to %d)\n", seqNo - last, last, seqNo);
					last = seqNo;
				}
#else
				valid = true;
#endif
			}
		}

		//
		// Start to decode packet
		//
		if (valid)
		{
			bool scanline = true;

			// Decode Header bits
			marker = (packet->head.rtp.protocol & 0x00800000) >> 23;
#if RTP_CHECK
			printf("[RTP] seqNo %d, Packet %d, marker %d, Rx length %d, timestamp 0x%08x\n", seqNo, payloadType, marker, len, packet->head.rtp.timestamp);
#endif

			//
			// Count the number of scanlines in the packet
			//
			while (scanline)
			{
				int more;
#if ENDIAN_SWAP
				endianswap16((uint16_t *)&packet->head.payload.line[scancount], sizeof(line_header)/2 );
#endif
				more = (packet->head.payload.line[scancount].offset & 0x8000) >> 15;
				if (!more) scanline =  false; // The last scanline
				scancount++;
			}

			//
			// Now we know the number of scanlines we can copy the data
			//
			int payloadoffset = sizeof(rtp_header) + 2 + (scancount * sizeof(line_header));
			int payload = 0;

			lastpacket = payloadoffset;
			for (int c=0;c<scancount; c++)
			{
				uint32_t os;
				uint32_t pixel;
				uint32_t length;

				os = payloadoffset + payload;
				pixel = ((packet->head.payload.line[c].offset & 0x7FFF)*2) + ((packet->head.payload.line[c].line_number & 0x7FFF) * (arg->width*2));
				length = packet->head.payload.line[c].length & 0xFFFF;

#if GST_1_FUDGE 
				memcpy(&arg->stream->bufferIn[pixel+1], &arg->stream->udpdata[os], length);
#else
				memcpy(&arg->stream->bufferIn[pixel], &arg->stream->udpdata[os], length);
#endif
				lastpacket += length;
				payload += length;
			}

			if (marker) receiving = false;

			scanline = true;
			scancount = 0;
		}
	}

	arg->yuvframe = arg->stream->bufferIn;
	arg->gpuAddr = arg->stream->gpuBuffer;

//	printf(">>> 0x%08x, 0x%08x \n", arg->stream->bufferIn, arg->stream->gpuBuffer);

	return 0;
}

static tx_data arg_rx;
bool rtpStream::Capture( void** cpu, void** cuda, unsigned long timeout )
{
	sched_param param;
	pthread_attr_t tattr;
	pthread_t rx;
	arg_rx.rgbframe = 0;
	arg_rx.gpuAddr = 0;
	arg_rx.width = mWidth;
	arg_rx.height = mHeight;
	arg_rx.stream = this;

#if RTP_THREADED
	// Elevate priority to get the RTP packets in quickly
  pthread_attr_init(&tattr);
  pthread_attr_getschedparam(&tattr, &param);
	param.sched_priority = 99;
  pthread_attr_setschedparam(&tattr, &param);

	// Start a thread so we can start capturing the next frame while transmitting the data
	pthread_create(&rx, &tattr, ReceiveThread, &arg_rx );

	// Wait for completion
	pthread_join(rx, 0 );
#else
	ReceiveThread(&arg_rx);
#endif
	*cpu = (void*)bufferIn;
	*cuda = (void*)gpuBuffer;
	return true;
}

int TransmitThread(void* data)
{
    rtp_packet packet;
	  tx_data *arg;
    char *yuv;
    int c=0;
    int n=0;

	  arg = (tx_data *)data;

    sequence_number=0;

    /* send a frame */
    pthread_mutex_lock(&arg->stream->mutex);
    {
		struct timeval NTP_value;
		int32_t time = 10000;

		for (c=0;c<(arg->height);c++)
		{
			int x,last = 0;
			if (c==arg->height-1) last=1;
				arg->stream->update_header((header*)&packet, c, last, time, RTP_SOURCE);

#if ENDIAN_SWAP
			endianswap32((uint32_t *)&packet, sizeof(rtp_header)/4);
			endianswap16((uint16_t *)&packet.head.payload, sizeof(payload_header)/2);
#endif
			n = sendto(arg->stream->mSockfdOut, (char *)&packet, 24+(arg->width*2), 0, (const sockaddr*)&arg->stream->mServeraddrOut, arg->stream->mServerlenOut);

			if (n < 0 ) 
			{
				printf("[RTP] Transmit socket failure fd=%d\n", arg->stream->mSockfdOut);
				return n;
			}
		}
    }
    pthread_mutex_unlock(&arg->stream->mutex);
    return 0;
}

// Arguments sent to thread
sched_param param;
pthread_t tx;
static tx_data arg_tx;

int rtpStream::Transmit(char* rgbframe, bool gpuAddr)
{
	arg_tx.rgbframe = rgbframe;
	arg_tx.gpuAddr = gpuAddr;
	arg_tx.width = mWidth;
	arg_tx.height = mHeight;
	arg_tx.stream = this;

#if RTP_THREADED
	// Start a thread so we can start capturing the next frame while transmitting the data
	pthread_join(tx, 0 );
	pthread_create(&tx, NULL, TransmitThread, &arg_tx );
    return 0; // Cant know the if the transmit was successfull if done in a thread
#else
	return TransmitThread(&arg_tx);
#endif
}

