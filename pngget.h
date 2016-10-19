#include <png.h>
png_structp png_ptr;
png_bytep * row_pointers;
void read_png_file(char* file_name);
void rgbtoyuv(int x, int y, char* yuv, char* rgb);

