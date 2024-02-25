typedef unsigned int uint32;
typedef unsigned long uint64;

void _start(uint32 *fb, uint64 size)
{
    uint64 offset;
    uint64 pixels;

    pixels = size / 4;

    for (offset = 0; offset < pixels; offset++)
    {
        *(fb + offset) = (offset % 256);
        *(fb + offset) += ((offset / 2) % 256) << 8;
        *(fb + offset) += ((offset / 4) % 256) << 16;
    }

    for (;;)
        ;
}
