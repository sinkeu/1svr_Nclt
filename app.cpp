#include <stdio.h>  
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>  
#include <sys/un.h>  
#include <sys/stat.h>
#include <sys/socket.h>  
#include <sys/un.h>  
#include <sys/mman.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <iostream>
#include <map>
using namespace std;

#define WIDTH           1280 ///< width of DDR buffer in pixels
#define HEIGHT          800 ///< height of DDR buffer in pixels
#define DDR_BUFFER_CNT  3    ///< number of DDR buffers per ISP stream

#define GETTIME(time) \
  { \
  struct timeval lTime; gettimeofday(&lTime,0); \
  *time=(lTime.tv_sec*1000000+lTime.tv_usec);   \
  }

typedef unsigned int uint32_t;
#pragma pack(1)
typedef struct  
{  
    //unsigned short    bfType;   
    unsigned int    bfSize;  
    unsigned short    bfReserved1;  
    unsigned short    bfReserved2;  
    unsigned int    bfOffBits;  
} ClBitMapFileHeader;  
  
typedef struct  
{  
    unsigned int  biSize;   
    int   biWidth;   
    int   biHeight;   
    unsigned short   biPlanes;   
    unsigned short   biBitCount;  
    unsigned int  biCompression;   
    unsigned int  biSizeImage;   
    int   biXPelsPerMeter;   
    int   biYPelsPerMeter;   
    unsigned int   biClrUsed;   
    unsigned int   biClrImportant;   
} ClBitMapInfoHeader;  
  
typedef struct   
{  
    unsigned char rgbBlue; //该颜色的蓝色分量   
    unsigned char rgbGreen; //该颜色的绿色分量   
    unsigned char rgbRed; //该颜色的红色分量   
    unsigned char rgbReserved; //保留值   
} ClRgbQuad;  
  
typedef struct  
{  
    int width;  
    int height;  
    int channels;  
    unsigned char* imageData;  
}ClImage;  
#pragma pack()

bool clSaveImage(char* path, ClImage* bmpImg)  
{  
    FILE *pFile;  
    unsigned short fileType;  
    ClBitMapFileHeader bmpFileHeader;  
    ClBitMapInfoHeader bmpInfoHeader;  
    int step;  
    int offset;  
    unsigned char pixVal = '\0';  
    int i, j;  
    ClRgbQuad* quad;  
  
    pFile = fopen(path, "wb");  
    if (!pFile)  
    {  
        return false;  
    }  
  
    fileType = 0x4D42;  
    fwrite(&fileType, sizeof(unsigned short), 1, pFile);  
  
    if (bmpImg->channels == 3)//24位，通道，彩图   
    {  
        step = bmpImg->channels*bmpImg->width;  
        offset = step%4;  
        if (offset != 4)  
        {  
            step += 4-offset;  
        }  
  
        bmpFileHeader.bfSize = bmpImg->height*step + 54;  
        bmpFileHeader.bfReserved1 = 0;  
        bmpFileHeader.bfReserved2 = 0;  
        bmpFileHeader.bfOffBits = 54;  
        fwrite(&bmpFileHeader, sizeof(ClBitMapFileHeader), 1, pFile);  
  
        bmpInfoHeader.biSize = 40;  
        bmpInfoHeader.biWidth = bmpImg->width;  
        bmpInfoHeader.biHeight = bmpImg->height;  
        bmpInfoHeader.biPlanes = 1;  
        bmpInfoHeader.biBitCount = 24;  
        bmpInfoHeader.biCompression = 0;  
        bmpInfoHeader.biSizeImage = bmpImg->height*step;  
        bmpInfoHeader.biXPelsPerMeter = 0;  
        bmpInfoHeader.biYPelsPerMeter = 0;  
        bmpInfoHeader.biClrUsed = 0;  
        bmpInfoHeader.biClrImportant = 0;  
        fwrite(&bmpInfoHeader, sizeof(ClBitMapInfoHeader), 1, pFile);  
  
        for (i=bmpImg->height-1; i>-1; i--)  
        {  
            for (j=0; j<bmpImg->width; j++)  
            {  
                pixVal = bmpImg->imageData[i*bmpImg->width*3+j*3];  
                fwrite(&pixVal, sizeof(unsigned char), 1, pFile);  
                pixVal = bmpImg->imageData[i*bmpImg->width*3+j*3+1];  
                fwrite(&pixVal, sizeof(unsigned char), 1, pFile);  
                pixVal = bmpImg->imageData[i*bmpImg->width*3+j*3+2];  
                fwrite(&pixVal, sizeof(unsigned char), 1, pFile);  
            }  
            if (offset!=0)  
            {  
                for (j=0; j<offset; j++)  
                {  
                    pixVal = 0;  
                    fwrite(&pixVal, sizeof(unsigned char), 1, pFile);  
                }  
            }  
        }  
    }  
    else if (bmpImg->channels == 1)//8位，单通道，灰度图   
    {  
        step = bmpImg->width;  
        offset = step%4;  
        if (offset != 4)  
        {  
            step += 4-offset;  
        }  
  
        bmpFileHeader.bfSize = 54 + 256*4 + bmpImg->width;  
        bmpFileHeader.bfReserved1 = 0;  
        bmpFileHeader.bfReserved2 = 0;  
        bmpFileHeader.bfOffBits = 54 + 256*4;  
        fwrite(&bmpFileHeader, sizeof(ClBitMapFileHeader), 1, pFile);  
  
        bmpInfoHeader.biSize = 40;  
        bmpInfoHeader.biWidth = bmpImg->width;  
        bmpInfoHeader.biHeight = bmpImg->height;  
        bmpInfoHeader.biPlanes = 1;  
        bmpInfoHeader.biBitCount = 8;  
        bmpInfoHeader.biCompression = 0;  
        bmpInfoHeader.biSizeImage = bmpImg->height*step;  
        bmpInfoHeader.biXPelsPerMeter = 0;  
        bmpInfoHeader.biYPelsPerMeter = 0;  
        bmpInfoHeader.biClrUsed = 256;  
        bmpInfoHeader.biClrImportant = 256;  
        fwrite(&bmpInfoHeader, sizeof(ClBitMapInfoHeader), 1, pFile);  
  
        quad = (ClRgbQuad*)malloc(sizeof(ClRgbQuad)*256);  
        for (i=0; i<256; i++)  
        {  
            quad[i].rgbBlue = i;  
            quad[i].rgbGreen = i;  
            quad[i].rgbRed = i;  
            quad[i].rgbReserved = 0;  
        }  
        fwrite(quad, sizeof(ClRgbQuad), 256, pFile);  
        free(quad);  
  
        for (i=bmpImg->height-1; i>-1; i--)  
        {  
            for (j=0; j<bmpImg->width; j++)  
            {  
                pixVal = bmpImg->imageData[i*bmpImg->width+j];  
                fwrite(&pixVal, sizeof(unsigned char), 1, pFile);  
            }  
            if (offset!=0)  
            {  
                for (j=0; j<offset; j++)  
                {  
                    pixVal = 0;  
                    fwrite(&pixVal, sizeof(unsigned char), 1, pFile);  
                }  
            }  
        }  
    }  
    fclose(pFile);  
  
    return true;  
}  

void *isp_svr_mmap(off_t phy_addr, size_t len)
{
    // Truncate physical address to a multiple of the page size, or mmap will fail.
    size_t pagesize = sysconf(_SC_PAGE_SIZE);
    off_t page_base = (phy_addr / pagesize) * pagesize;
    off_t page_offset = phy_addr - page_base;

    int fd = open("/dev/mem", O_SYNC);
    void *mem = mmap(NULL, page_offset + len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, page_base);
    if (mem == MAP_FAILED) {
        printf("can not map memory\n");
        return NULL;
    }

    return mem;
}

int main(int argc, char *argv[])
{
    int fd;
    int fdmaIdx;
    uint32_t addr = 0;
    int times = 0, ret = 0, cnt = 0;
    int width, height, size;
    char buffer[64] = {0};
    char *vir = NULL;
    ClImage image;
    struct sockaddr_in servaddr;
    map<uint32_t, char *> addrMap;
    map<uint32_t, char *>::iterator it;
    unsigned long lTimeStart = 0, lTimeEnd = 0, lTimeDiff = 0;

    if(argc != 5) {
        printf("Usage: %s 0 1280 800 10\n", argv[0]);
        printf("Usage: %s 3 640 784 10\n", argv[0]);
        printf("Usage: %s <channel> <width> <height> <loop times>\n", argv[0]);
        return 0;
    }

    fdmaIdx = atoi(argv[1]);
    width = atoi(argv[2]);
    height = atoi(argv[3]);
    times = atoi(argv[4]);
    size = width * height;

    image.width = width;
    image.height = height;
    image.channels = 1;
    image.imageData = (unsigned char *)malloc(size*3);

    //creat unix socket  
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        printf("cannot create communication socket\n");
        return 1;
    }

    bzero(&servaddr , sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(61080);
    ret = inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
    if(ret == -1)
    {
        printf("inet_pton to 127.0.0.1 error\n");
        close(fd);
        return 1; 
    }

    //connect server
    ret = connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if(ret == -1)
    {
        printf("cannot connect to the server\n");
        close(fd);
        return 1; 
    }

    //tell server fdma index we use
    write(fd, &fdmaIdx, sizeof(fdmaIdx));
 
    GETTIME(&lTimeStart);
    for(int i = 0; i < times; i++) {

        //recv buffer physical address
        read(fd, &addr, sizeof(addr));
        //printf("app get addr from server %x\n", addr);

        //map to virtual address
        if(addr) {
            it = addrMap.find(addr);
            if(it == addrMap.end()) {
                vir = (char *)isp_svr_mmap(addr, size);
                addrMap[addr] = vir;
            } else {
                vir = it->second;
            }
        }

        /*
        for(it = addrMap.begin(); it != addrMap.end(); it++)
            printf("%x:%p\n", it->first, it->second);
        */

        if(vir) {
            memcpy(image.imageData, vir, size);
            sprintf(buffer, "double_2by1_%d.bmp", cnt++);
            clSaveImage(buffer, &image);
        }

        //tell server we process done
        write(fd, &fdmaIdx, sizeof(fdmaIdx));

        GETTIME(&lTimeEnd);
        lTimeDiff = (lTimeEnd - lTimeStart);
        lTimeStart = lTimeEnd;

        printf("1 frame took %lu usec (%5.2ffps)\n",lTimeDiff,
            (1000000.0)/((float)lTimeDiff));
    }

    close(fd);

    return 0;
}
