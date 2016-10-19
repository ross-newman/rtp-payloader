rtpstream: rtpstream.c pngget.c
	gcc -o rtpstream rtpstream.c pngget.c -I. -lpng
