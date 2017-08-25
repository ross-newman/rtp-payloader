rtpstream: rtpstream.c pngget.c
	g++ -o transmit rtpStream.cpp example.cpp pngget.c -I. -lpng -lpthread
