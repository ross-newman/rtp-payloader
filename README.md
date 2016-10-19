
![Abaco stripe](docs/abaco-banner.png)

# Real Time Protocol in C
## Payloader example
This is a RAW (YUV) Real Time Protocol pay-loader written in C. This example is send only to recieve the data you can use the gstreamer pipeline below.

> **NOTE** : This example has been tested on 64 bit ARM. Target hardware was the Nvidia Jetson TX1 and Abaco Systems GVC1000.

# Installation
clone the code.

    git clone https://github.com/ross-abaco/rtp-payloader
Build the example

    cd rtpstream
    make
Run the example

    ./rtpstream
Catch the stream using the gstreamer src pipeline in the section below.

> **NOTE** : This example uses the lena (.png) test image as the source of the video stream. You can replace lena with your own image or use another source for the video data.

## gstreamer YUV streaming examples
Use this pipeline as a tes payloader to make sure gstreamer is working:

    gst-launch-1.0 videotestsrc num_buffers ! video/x-raw, format=UYVY, framerate=25/1, width=640, height=480 ! queue ! rtpvrawpay ! udpsink host=127.0.0.1 port=5004

Use this pipeline to capture the stream:

    gst-launch-1.0 udpsrc port=5004 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)RAW, sampling=(string)YCbCr-4:2:2, depth=(string)8, width=(string)640, height=(string)480, payload=(int)96" ! queue ! rtpvrawdepay ! queue ! xvimagesink sync=false
    
Gstreamer running with test image (480x480):

![Lena test image](docs/lena-test.png)
