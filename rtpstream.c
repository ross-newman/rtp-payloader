/* Example RTP packet from wireshark
      Real-Time Transport Protocol
      10.. .... = Version: RFC 1889 Version (2)
      ..0. .... = Padding: False
      ...0 .... = Extension: False
      .... 0000 = Contributing source identifiers count: 0
      0... .... = Marker: False
      Payload type: DynamicRTP-Type-96 (96)
      Sequence number: 34513
      Timestamp: 2999318601
      Synchronization Source identifier: 0xdccae7a8 (3704285096)
      Payload: 000003c000a08000019e00a2000029292929f06e29292929...
*/

/*

Gstreamer1.0 working example UYVY streaming
===========================================
gst-launch-1.0 videotestsrc num_buffers ! video/x-raw, format=UYVY, framerate=25/1, width=640, height=480 ! queue ! rtpvrawpay ! udpsink host=127.0.0.1 port=5004

gst-launch-1.0 udpsrc port=5004 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)RAW, sampling=(string)YCbCr-4:2:2, depth=(string)8, width=(string)480, height=(string)480, payload=(int)96" ! queue ! rtpvrawdepay ! queue ! xvimagesink sync=false


Use his program to stream data to the udpsc example above on the tegra X1 

*/


#include <byteswap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include "pngget.h"

#define ARM                   0    /* Perform endian swap */
#define RTP_VERSION           0x2  /* RFC 1889 Version 2 */
#define RTP_PADDING           0x0
#define RTP_EXTENSION         0x0
#define RTP_MARKER            0x0
#define RTP_PAYLOAD_TYPE      0x60 /* 96 Dynamic Type */
#define RTP_SOURCE            0x12345678 /* Sould be unique */
#define RTP_FRAMERATE         25

#define Hz90                  90000
#define NUM_LINES_PER_PACKET  1 /* can have more that one line in a packet */
#define STREAM_HEIGHT 480
#define STREAM_WIDTH 480
#define BUFSIZE STREAM_WIDTH * 3 /* allow for RGB data */

static unsigned long sequence_number;

/* 12 byte RTP Raw video header */
typedef struct
{
  int32_t protocol;
  int32_t timestamp;
  int32_t source;
} rtp_header;


typedef struct  __attribute__((__packed__))
{
  int16_t length;
  int16_t line_number;
  int16_t offset;
} line_header;

typedef struct __attribute__((__packed__))
{
  int16_t extended_sequence_number;
  line_header line[NUM_LINES_PER_PACKET];
} payload_header;


typedef struct  __attribute__((__packed__))
{
  rtp_header rtp;
  payload_header payload;
  int8_t pad;
} header;

typedef struct 
{
  header head;
  char data[BUFSIZE];
} rtp_packet;

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

void update_header(header *packet, int line, int last, int32_t timestamp, int32_t source)
{
  bzero((char *)packet, sizeof(header));
  packet->rtp.protocol = RTP_VERSION << 30;
  packet->rtp.protocol = packet->rtp.protocol | RTP_PAYLOAD_TYPE << 16;
  packet->rtp.protocol = packet->rtp.protocol | sequence_number++;
  /* leaving other fields as zero TODO Fix*/
  packet->rtp.timestamp = timestamp += (Hz90 / RTP_FRAMERATE);
  packet->rtp.source = source;
  packet->payload.extended_sequence_number = 0; /* TODO : Fix extended seq numbers */
  packet->payload.line[0].length = BUFSIZE;
  packet->payload.line[0].line_number = line;
  packet->payload.line[0].offset = 0;
  if (last==1)
  {
    packet->rtp.protocol = packet->rtp.protocol | 1 << 23;
  } 
#if 0
  printf("0x%x, 0x%x, 0x%x \n", packet->rtp.protocol, packet->rtp.timestamp, packet->rtp.source);
  printf("0x%x, 0x%x, 0x%x \n", packet->payload.line[0].length, packet->payload.line[0].line_number, packet->payload.line[0].offset);
#endif
}

#if ARM
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

int main(int argc, char **argv) {
    int sockfd, portno, n, c , frame = 0;
    int serverlen, move=0;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    const char hostname[] = "127.0.0.1";
    rtp_packet packet;
    char *yuv;
    png_byte* row;
    
    yuv = malloc((STREAM_WIDTH * STREAM_HEIGHT) * 2);

    printf("rtp-example, Author ross@rossnewman.com\n");
    sequence_number=0;
    
    read_png_file("lenna-lg.png");

    /* Broadcast the stream to port 5004 */
    portno = 5004;

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    /* get a message from the user */
    bzero(packet.data, BUFSIZE);

    /* send the message to the server */
    serverlen = sizeof(serveraddr);
    
    /* Loop frames forever */
    while (1)
    {
      struct timeval NTP_value;
      int32_t time = 10000;
      
      
      for (c=0;c<(STREAM_HEIGHT);c++)
      {
        int x,last = 0;
        if (c==STREAM_HEIGHT-1) last=1;
        update_header((header*)&packet, c, last, time, RTP_SOURCE);

#if 1
        /* update scan line with white noise UYUV format */
        for (x=0;x<STREAM_WIDTH;x++)
        {
          packet.data[x*2]=rand() % 255;
          packet.data[(x*2)+1]=0x80;
        }
#else
        png_byte* row = row_pointers[c];

        rgbtoyuv(STREAM_HEIGHT, STREAM_WIDTH, packet.data, &row[move]);
#endif

#if ARM
        endianswap32((uint32_t *)&packet, sizeof(rtp_header)/4);
        endianswap16((uint16_t *)&packet.head.payload, sizeof(payload_header)/2);
#endif
        n = sendto(sockfd, (char *)&packet, sizeof(rtp_packet), 0, (void*)&serveraddr, serverlen);
        if (n < 0) 
          error("ERROR in sendto");
      }
      
#if 0
      /* move the image (png must have extra byte as the second image is green)  */
      move+=3;
      if (move==STREAM_WIDTH*3) move=0;
#endif
      /* approximatly 24 frames a second */
      usleep(1000000 / RTP_FRAMERATE);
      time += (Hz90 / RTP_FRAMERATE);
      printf("Sent frame %d\n", frame++);
    }
    
    free(yuv);
    
    return 0;
}
