#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>                  /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>   // 시스템 호출 통한 입출력 제어

#include <linux/fb.h> // 프레임버퍼 관련 구조체 및 상수 정의

#include <asm/types.h>               /* for videodev2.h */
#include <linux/videodev2.h>  // v4l2 관련 정의 및 구조체

#include "bmpHeader.h"

#define FBDEV        "/dev/fb0"      // 프레임버퍼 장치 파일 경로. 프레임 버퍼를 위한 디바이스 파일
#define VIDEODEV    "/dev/video0"     .. 비디오 장치 파일 경로
#define WIDTH       800               /* 캡쳐받을 영상의 크기 */
#define HEIGHT      600                

#define NUMCOLOR    3


/* Video4Linux에서 사용할 영상 저장을 위한 버퍼 */
struct buffer {
    void * start;    // 버퍼의 시작주소
    size_t length;    // 버퍼의 크기
};

static short *fbp   	      = NULL;         /* 프레임버퍼의 MMAP를 위한 변수 */
struct buffer *buffers        = NULL;
static unsigned int n_buffers = 0;
static struct fb_var_screeninfo vinfo;                   /* 프레임버퍼의 정보 저장을 위한 구조체 */

static void mesg_exit(const char *s) // 오류발생시 오류메시지 출력하고 프로그램 종료하는 함수
{
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

static int xioctl(int fd, int request, void *arg) // 시스템호출 반복수행해서 신호방해로 실패할경우 다시시도하는함수. 이를통해 v4l2장치에 명령전송
{
    int r;
    do r = ioctl(fd, request, arg); while(-1 == r && EINTR == errno);
    return r;
}

/* unsigned char의 범위를 넘어가지 않도록 경계 검사를 수행다. */
//rgb 변환과정에서 값이 0~255 넘어가지않도록 제한
extern inline int clip(int value, int min, int max)
{
    return(value > max ? max : value < min ? min : value);
}

									  
void saveImage(unsigned char *inimg) // 입력받은 이미지데이터를 bmp로 저장하는 함수
{
    RGBQUAD palrgb[256]; // 팔레트
    FILE *fp;
    BITMAPFILEHEADER bmpFileHeader; // bmp파일헤더
    BITMAPINFOHEADER bmpInfoHeader; // 이미지정보헤더

    /* BITMAPFILEHEADER 구조체에 BMP 파일 정보 설정 */
    memset(&bmpFileHeader, 0, sizeof(BITMAPFILEHEADER));
    bmpFileHeader.bfType = 0x4d42; /* (unsigned short)('B' | 'M' << 8)과 같다. */
    /* 54(14 + 40)바이트의 크기 */
    bmpFileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmpFileHeader.bfOffBits += sizeof(RGBQUAD) * 256;
    bmpFileHeader.bfSize = bmpFileHeader.bfOffBits;
    bmpFileHeader.bfSize += WIDTH*HEIGHT*NUMCOLOR;

    /* BITMAPINFOHEADER 구조체에 BMP 이미지 정보 설정 */
    memset(&bmpInfoHeader, 0, sizeof(BITMAPINFOHEADER));
    bmpInfoHeader.biSize = sizeof(BITMAPINFOHEADER); /* 40 바이트의 크기 */
    bmpInfoHeader.biWidth = WIDTH;
    bmpInfoHeader.biHeight = HEIGHT;
    bmpInfoHeader.biPlanes = 1;
    bmpInfoHeader.biBitCount = NUMCOLOR*8;
    bmpInfoHeader.SizeImage = WIDTH*HEIGHT*bmpInfoHeader.biBitCount/8;
    bmpInfoHeader.biXPelsPerMeter = 0x0B12;
    bmpInfoHeader.biYPelsPerMeter = 0x0B12;

    /* 저장을 위한 이미지 파일 오픈 */
    if((fp = fopen("capture.bmp", "wb")) == NULL) {
        fprintf(stderr, "Error : Failed to open file...\n");
        exit(EXIT_FAILURE);
    }

    /* BMP 파일(BITMAPFILEHEADER) 정보 저장 */
    fwrite((void*)&bmpFileHeader, sizeof(bmpFileHeader), 1, fp);

    /* BMP 이미지(BITMAPINFOHEADER) 정보 저장 */
    fwrite((void*)&bmpInfoHeader, sizeof(bmpInfoHeader), 1, fp);

    /* 팔렛트(RGBQUAD) 정보 저장 */
    fwrite(palrgb, sizeof(RGBQUAD), 256, fp);

    /* BMP 데이터 저장 */
    fwrite(inimg, sizeof(unsigned char), WIDTH*HEIGHT*NUMCOLOR, fp);

    fclose(fp);
}

#define NO_OF_LOOP 1

// 카메라로부터 캡쳐한 YUYV 영상을 RGB로 변환하고, 프레임버퍼에 쓰고, BMP파일로 저장하는 함수
static void process_image(const void *p) 
{
    unsigned char* in =(unsigned char*)p;
    int width = WIDTH;
    int height = HEIGHT;
    int istride = WIDTH*2;          /* 이미지의 폭을 넘어가면 다음 라인으로 내려가도록 설정 */
    int x, y, j;
    int y0, u, y1, v, r, g, b;
    unsigned short pixel;
    long location = 0, count = 0;
	unsigned char* inimg = (unsigned char*)malloc(NUMCOLOR*WIDTH*HEIGHT*sizeof(unsigned char)); // 이미지 저장 위한 변수

	for(y = 0; y < height; y++, count = 0) {
        for(j = 0; j < vinfo.xres * 2; j += 4) {
            if(j >= width*2) {                 // 현재의 화면에서 이미지를 넘어서는 빈 공간을 처리
              location++; location++;
              continue;
            }
            // YUYV 성분을 분리 
            y0 = in[j];
            u = in[j + 1] - 128;
            y1 = in[j + 2];
            v = in[j + 3] - 128;

            // YUV를 RGB로 전환 
            r = clip((298 * y0 + 409 * v + 128) >> 8, 0, 255);
            g = clip((298 * y0 - 100 * u - 208 * v + 128) >> 8, 0, 255);
            b = clip((298 * y0 + 516 * u + 128) >> 8, 0, 255);
            pixel =((r>>3)<<11)|((g>>2)<<5)|(b>>3);                 // 16비트 컬러로 전환 
            fbp[location++] = pixel;

			// BMP 이미지 데이터 
			inimg[(height-y-1)*width*NUMCOLOR+count++] = b;
			inimg[(height-y-1)*width*NUMCOLOR+count++] = g;
			inimg[(height-y-1)*width*NUMCOLOR+count++] = r;
			
			// YUV를 RGB로 전환 
            r = clip((298 * y1 + 409 * v + 128) >> 8, 0, 255);
            g = clip((298 * y1 - 100 * u - 208 * v + 128) >> 8, 0, 255);
            b = clip((298 * y1 + 516 * u + 128) >> 8, 0, 255);
            pixel =((r>>3)<<11)|((g>>2)<<5)|(b>>3);                 // 16비트 컬러로 전환 
            fbp[location++] = pixel;

			// BMP 이미지 데이터 
			inimg[(height-y-1)*width*NUMCOLOR+count++] = b;
			inimg[(height-y-1)*width*NUMCOLOR+count++] = g;
			inimg[(height-y-1)*width*NUMCOLOR+count++] = r;
        }
		in += istride;	
	}
	// 이미지 데이터를 bmp 파일로 저장한다.
	saveImage(inimg);
}

// v4l2 버퍼에서 한 프레임 읽어와 process_image 함수로 처리
// 버퍼에서 데이터 읽어오고, 사용 후 다시 버퍼를 큐에 넣음
static int read_frame(int fd)
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if(-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
        switch(errno) {
        case EAGAIN: return 0;
        case EIO:
            /* Could ignore EIO, see spec. */
        default: mesg_exit("VIDIOC_DQBUF");
        }
    }

    process_image(buffers[buf.index].start);

    if(-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        mesg_exit("VIDIOC_QBUF");

    return 1;
}

// 프레임을 100번 읽는 루프
static void mainloop(int fd)
{
    unsigned int count = 100;
    while(count-- > 0) {
        for(;;) {
            fd_set fds;
            struct timeval tv;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            int r = select(fd + 1, &fds, NULL, NULL, &tv);
            if(-1 == r) {
                if(EINTR == errno) continue;
                mesg_exit("select");
            } else if(0 == r) {
                fprintf(stderr, "select timeout\n");
                exit(EXIT_FAILURE);
            }

            if(read_frame(fd)) break;
        }
    }
}

// 스트리밍을 시작
// 큐에 버퍼를 채운 후, VIDIOC_STREAMON 명령으로 스트리밍 시작
static void start_capturing(int fd)
{
    for(int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
				memset(&buf, 0, sizeof(buf));
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;
        if(-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            mesg_exit("VIDIOC_QBUF");
    }
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(-1 == xioctl(fd, VIDIOC_STREAMON, &type))
        mesg_exit("VIDIOC_STREAMON");
}

// 메모리매핑
static void init_mmap(int fd)
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count       = 4;
    req.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory      = V4L2_MEMORY_MMAP;
    if(-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if(EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", VIDEODEV);
            exit(EXIT_FAILURE);
        } else {
            mesg_exit("VIDIOC_REQBUFS");
        }
    }

    if(req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", VIDEODEV);
        exit(EXIT_FAILURE);
    }

    buffers = calloc(req.count, sizeof(*buffers));
    if(!buffers) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    for(n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = n_buffers;
        if(-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
            mesg_exit("VIDIOC_QUERYBUF");

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, buf.m.offset);
        if(MAP_FAILED == buffers[n_buffers].start)
            mesg_exit("mmap");
    }
}

// 카메라 장치를 초기화
// 장치가 v4l2를 지원하는지 확인한 후, 스트리밍 위한 메모리 매핑 설정
// 카메라 포맷 YUYV로 설정하고, 영상을 800*600 크기로 설정
static void init_device(int fd)
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;

    if(-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if(EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", VIDEODEV);
            exit(EXIT_FAILURE);
        } else {
            mesg_exit("VIDIOC_QUERYCAP");
        }
    }

    if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n",
                         VIDEODEV);
        exit(EXIT_FAILURE);
    }

    if(!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", VIDEODEV);
        exit(EXIT_FAILURE);
    }

    /* Select video input, video standard and tune here. */
    memset(&cropcap, 0, sizeof(cropcap));
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */
        xioctl(fd, VIDIOC_S_CROP, &crop);
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if(-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
        mesg_exit("VIDIOC_S_FMT");

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if(fmt.fmt.pix.bytesperline < min)
        fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if(fmt.fmt.pix.sizeimage < min)
        fmt.fmt.pix.sizeimage = min;

    init_mmap(fd);
} 

// 프레임버퍼장치와 비디오장치 열고 장치의 정보를 읽어옴
// 프레임버퍼와 비디오메모리 매핑한 후 비디오 장치를 초기화하고 스트리밍을 시작
// mainloop 함수에서 반복적으로 프레임 처리하며 스트리밍 중단 후 메모리 정리하고 장치를 닫음
int main(int argc, char **argv)
{
    int fbfd = -1;              /* 프레임버퍼의 파일 디스크립터 */
    int camfd = -1;		/* 카메라의 파일 디스크립터 */

    /* 프레임버퍼 열기 */
    fbfd = open(FBDEV, O_RDWR);
    if(-1 == fbfd) {
        perror("open( ) : framebuffer device");
        return EXIT_FAILURE;
    }

    /* 프레임버퍼의 정보 가져오기 */
    if(-1 == ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information.");
        return EXIT_FAILURE;
    }

    /* mmap( ) : 프레임버퍼를 위한 메모리 공간 확보 */
    long screensize = vinfo.xres * vinfo.yres * 2;
    fbp = (short *)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if(fbp == (short*)-1) {
        perror("mmap() : framebuffer device to memory");
        return EXIT_FAILURE;
    }
    
    memset(fbp, 0, screensize);
    
    /* 카메라 장치 열기 */
    camfd = open(VIDEODEV, O_RDWR | O_NONBLOCK, 0);
    if(-1 == camfd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n",
                         VIDEODEV, errno, strerror(errno));
        return EXIT_FAILURE;
    }

    init_device(camfd);

    start_capturing(camfd);

    mainloop(camfd);

    /* 캡쳐 중단 */
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(-1 == xioctl(camfd, VIDIOC_STREAMOFF, &type))
        mesg_exit("VIDIOC_STREAMOFF");

    /* 메모리 정리 */
    for(int i = 0; i < n_buffers; ++i)
        if(-1 == munmap(buffers[i].start, buffers[i].length))
            mesg_exit("munmap");
    free(buffers);

    munmap(fbp, screensize);

    /* 장치 닫기 */
    if(-1 == close(camfd) && -1 == close(fbfd))
        mesg_exit("close");

    return EXIT_SUCCESS; 
}
