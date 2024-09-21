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
#include <sys/ioctl.h>

#include <linux/fb.h>

#include <asm/types.h>               /* for videodev2.h */
#include <linux/videodev2.h>

#define FBDEV        "/dev/fb0"      /* 프레임 버퍼를 위한 디바이스 파일 */
#define VIDEODEV    "/dev/video0"
#define WIDTH       800               /* 캡쳐받을 영상의 크기 */
#define HEIGHT      600                

/* Video4Linux에서 사용할 영상 저장을 위한 버퍼 */
struct buffer {
    void * start;
    size_t length;
};

static short *fbp   	      = NULL;         /* 프레임버퍼의 MMAP를 위한 변수 */
struct buffer *buffers        = NULL;
static unsigned int n_buffers = 0;
static struct fb_var_screeninfo vinfo;                   /* 프레임버퍼의 정보 저장을 위한 구조체 */

static void mesg_exit(const char *s)
{
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

static int xioctl(int fd, int request, void *arg)
{
    int r;
    do r = ioctl(fd, request, arg); while(-1 == r && EINTR == errno);
    return r;
}

/* unsigned char의 범위를 넘어가지 않도록 경계 검사를 수행다. */
extern inline int clip(int value, int min, int max)
{
    return(value > max ? max : value < min ? min : value);
}

static void process_image(const void *p)
{
	// p : 카메라에서 읽어들인 YUYV형식의 프레임데이터. 이를 unsigned char*로 변환하여 처리할 수 있게 준비
    unsigned char* in =(unsigned char*)p;
    int width = WIDTH;
    int height = HEIGHT;
    int istride = WIDTH*2;          /* 이미지의 폭을 넘어가면 다음 라인으로 내려가도록 설정. istride는 한 라인의 크기인데, YUYV는 1픽셀당 2바이트라서 한 라인의 데이터 크기가 WIDTH*2 */ 
    int x, y, j;
    int y0, u, y1, v, r, g, b;
    unsigned short pixel;
    long location = 0; // long은 32비트시스템에서 4바이트, 64비트시스템에서 8바이트. 현재 픽셀이 프레임버퍼 내에서 저장될 위치를 나타냄. 
	// 프레임버퍼(fbp) 는 화면의 픽셀 데이터를 순차적으로 저장하는 메모리공간임. 화면의 첫번째 픽셀은 fbp[0]에, 두번째 픽셀은 fbp[1]에 저장됨. 각 픽셀은 16비트 rgb 데이터 사용하고, 이 데이터는 unsigned short로 저장
	
    for(y = 0; y < height; ++y) { // 프레임의 각 라인 처리
        for(j = 0, x = 0; j < vinfo.xres * 2; j += 4, x += 2) { // 각 라인의 픽셀을 처리 (한번에 2픽셀씩 처리) YUYV형식이 2픽셀당 4바이트 사용하기 때문에. (j :  한번에 4바이트처리해서 4씩증가, x :  한번에 2픽셀씩 처리해서 2증가)
            if(j >= width*2) {                 /* 현재의 화면에서 이미지를 넘어서는 빈 공간을 처리 */
				// width*2는 화면 너비에 따라 한 줄에 저장될 픽셀 데이터의 양을 계산. *2는 YUYV포맷에서 두픽셀이 4바이트 차지하므로 실제 픽셀 수와 상관없이 각 라인에서 사용된 메모리 양 나타냄
				// 따라서 이 조건이 참이면 현재 픽셀이 프레임버퍼의 화면 밖에 있는 픽셀이란것을 의미
                 location++; location++; // location++을 통해 화면의 남은부분 건너뜀 (두번호출한건 YUYV에서 한 픽셀이 두바이트 차지하므로, 빈공간 건너뛸 때 두 바이트씩 건너뛰어야 하기 때문에
                 continue;
            }
            /* YUYV 성분을 분리 */
			// in은 1바이트단위로 이동가능한, YUYV프레임데이터를 가리키는 포인터.
			// YUYV형식은 2픽셀당 Y,U,V 값을 공유하며, 2픽셀을 처리할 때 마다 Y0,U,Y1,V 값으로 변환됨
            y0 = in[j]; // 첫번째픽셀의 Y값
            u = in[j + 1] - 128; // 두 픽셀이 공유하는 U값. (색상 차이 성분, 중앙값은 128)
            y1 = in[j + 2]; // 두번째 픽셀의 Y값
            v = in[j + 3] - 128; // 두 픽셀이 공유하는 V값 (색상차이성분, 중앙값은 128)

            /* YUV를 RGB로 전환 */
			// YUV to RGB 변환공식으로 YUV값을 RGB로 변환
            r = clip((298 * y0 + 409 * v + 128) >> 8, 0, 255);
            g = clip((298 * y0 - 100 * u - 208 * v + 128) >> 8, 0, 255);
            b = clip((298 * y0 + 516 * u + 128) >> 8, 0, 255);

			// 변환된 r,g,b 값을 16비트 rgb565형식으로 변환하여 프레임버퍼에 기록 (unsigned short인 pixel은 2바이트)
            pixel =((r>>3)<<11)|((g>>2)<<5)|(b>>3);                 /* 16비트 컬러로 전환 */
            fbp[location++] = pixel; //fbp는 short*타입. 프레임버퍼의 MMAP를 위한 전역변수

            /* YUV를 RGB로 전환 */
            r = clip((298 * y1 + 409 * v + 128) >> 8, 0, 255);
            g = clip((298 * y1 - 100 * u - 208 * v + 128) >> 8, 0, 255);
            b = clip((298 * y1 + 516 * u + 128) >> 8, 0, 255);
            pixel =((r>>3)<<11)|((g>>2)<<5)|(b>>3);                 /* 16비트 컬러로 전환 */
            fbp[location++] = pixel; // 같은방식으로 두번째 픽셀 y1을 처리하고 프레임버퍼에 기록
        };
        in += istride; // 한 라인을 다 처리하면 다음라인으로 이동
    };
}


//버퍼 큐에서 하나의 버퍼를 가져옴 (VIDIOC_DQBUF).
//가져온 버퍼의 데이터를 처리 (process_image).
//처리한 버퍼를 다시 큐에 반환 (VIDIOC_QBUF).
//이를 통해 실시간으로 비디오 프레임을 처리할 수 있게 함. -> 이 과정 반복으로 비디오스트리밍처럼 프레임이 연속적으로 처리됨
static int read_frame(int fd)
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if(-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) { // VIDIOC_DQBUF : 큐에서 사용가능한 버퍼를 제거. fd는 비디오장치 파일디스크립터,  buf는 가져올 버퍼의 정보 저장할 구조체
        switch(errno) {
        case EAGAIN: return 0; // 데이터 아직 준비 x란 뜻으로 0반환하여 재시도
        case EIO: // 입출력에러일때는 종료
            /* Could ignore EIO, see spec. */
        default: mesg_exit("VIDIOC_DQBUF");
        }
    }

    process_image(buffers[buf.index].start); // buffers[buf.index].start는 큐에서 꺼낸 버퍼에 저장된 프레임 데이터. 

    if(-1 == xioctl(fd, VIDIOC_QBUF, &buf)) // 큐에서 가져온 버퍼는 데이터 처리 후 다시 큐로 반환하여 장치가 다음 프레임데이터 기록할 수 있도록 VIDIOC_QBUF  명령 사용해 버퍼를 다시 큐에 등록
        mesg_exit("VIDIOC_QBUF");

    return 1; // 데이터처리 성공적으로 끝나면 1 반환하여 상위루프에서 프레임 성공적으로 처리했음을 알림
}

static void mainloop(int fd)
{
    unsigned int count = 100;
    while(count-- > 0) {
        for(;;) {
            fd_set fds; // 파일디스크립터 집합 관리하는 구조체
            struct timeval tv; // select 함수의 대기시간 위한 구조체

            FD_ZERO(&fds); // 초기화
            FD_SET(fd, &fds); // 카메라 장치의 파일디스크립터(fd. 메인함수의camfd)를 집합(fds) 에 추가 -> 데이터 들어오는지 감시 가능

            /* Timeout. */
            tv.tv_sec = 2; // 초단위 타임아웃
            tv.tv_usec = 0; // 마이크로초단위 타임아웃은 설정x

			// select : 특정조건충족시까지 기다리면서 다른작업수행 (blocking을 피함)
            int r = select(fd + 1, &fds, NULL, NULL, &tv); //첫번째인자:집합중 가장 큰 값에 1더한값 / 두번째인자:읽기 감시할 집합 / 세번째:쓰기감지,네번째:예외감지 / 다섯번째:최대대기시간 
            if(-1 == r) {
                if(EINTR == errno) continue;
                mesg_exit("select");
            } else if(0 == r) { // 타임아웃시 처리
                fprintf(stderr, "select timeout\n");
                exit(EXIT_FAILURE);
            }

            if(read_frame(fd)) break;  // 데이터가 오면 readframe
        }
    }
}

//V4L2 장치에서 비디오 스트리밍 시작 위해 버퍼를 큐에 등록하고, 스트리밍을 시작하는 과정을 처리하는 코드
static void start_capturing(int fd)
{
	// 사용 가능한 버퍼 수 만큼 반복해서 버퍼를 큐에 등록
    for(int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;
		// 각 버퍼를 큐에 등록. 드라이버에 이 버퍼를 사용하도록 알림
		// 큐에 등록된 버퍼는 장치가 캡처한 비디오 프레임을 기록하기 위한 준비된 공간
        if(-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            mesg_exit("VIDIOC_QBUF");
    }
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 버퍼 타입 설정하여 비디오스트리밍 시작 명시
    if(-1 == xioctl(fd, VIDIOC_STREAMON, &type)) // V4L2 드라이버에 스트리밍 시작하라고 알림
        mesg_exit("VIDIOC_STREAMON");
}


// 비디오장치에서 4개의 버퍼를 요청하고, 그 버퍼를 mmap로 프로세스 메모리공간에 연결하는 함수
// 이 함수로 장치에서 수신한 데이터가 사용자 프로그램에서 직접 접근할 수 있는 형태로 메모리 매핑됨
// 버퍼가 왜 여러개 필요한가? -> 하나의 버퍼만 사용시 버퍼가 잠기기때문에 동시에 읽기 쓰기가 불가능 (프레임손실)
static void init_mmap(int fd)
{
	//v4l2_requestbuffers 구조체를 이용하여 버퍼를 요청 (비디오 장치가 제공할 버퍼의 개수와 버퍼 타입을 정의)
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count       = 4; // 4개의 버퍼 요청
    req.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 버퍼가 비디오 캡쳐용임을 지정
    req.memory      = V4L2_MEMORY_MMAP; // 메모리 매핑방식으로 이 버퍼를 사용할것임을 나타냄
    if(-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {  // VIDIOC_REQBUFS 는 v4l2장치에서 드라이버에 메모리맵 I/O를 위한 버퍼 공간 요청 명령 (req.count로 요청된 개수만큼 버퍼를 할당받으려함)
        if(EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", VIDEODEV);
            exit(EXIT_FAILURE);
        } else {
            mesg_exit("VIDIOC_REQBUFS");
        }
    }

    if(req.count < 2) { // 버퍼가 2개보다 적게 할당되면 종료
        fprintf(stderr, "Insufficient buffer memory on %s\n", VIDEODEV);
        exit(EXIT_FAILURE);
    }

    buffers = calloc(req.count, sizeof(*buffers)); // 요청한 버퍼 개수만큼 메모리를 할당.req.count개의 buffer구조체 저장할 공간 할당
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
		// xiotcl 실행 전 buf 구조체에 type, memory, index를 지정하는 이유 : VIDIOC_QUERYBUF 명령이 정확히 어느 버퍼에 대한 정보 요청하는지를 V4L2 드라이버에 알려주기 위해서.
		
        if(-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) // VIDIOC_QUERYBUF : 드라이버로부터 각 버퍼의 메타데이터(버퍼의 크기, 위치 등) 가져옴 즉 fd에서 buf로 데이터를 가져와서 저장
            mesg_exit("VIDIOC_QUERYBUF");
		

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, buf.m.offset); // 장치드라이버가 관리하는 버퍼를 사용자 프로세스의 메모리공간에 매핑. buf.m.offset은 드라이버가 관리하는 물리메모리의 시작 위치
        if(MAP_FAILED == buffers[n_buffers].start)
            mesg_exit("mmap");
    }
}

static void init_device(int fd)
{
    struct v4l2_capability cap;

	// 캡처할 수 있는 영역에 대한 정보 가짐	
    struct v4l2_cropcap cropcap;
	// 실제 영상 크롭(자르기) 작업 정의
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;


// VIDIOC_QUERYCAP 는 현재장치(fd. 즉 pi camera) v4l2를 지원하는지 조사)
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
	//  카메라의 기본 캡처 영역을 알아내고(기본 크롭 영역 확인), 이를 기본 영역으로 설정
    memset(&cropcap, 0, sizeof(cropcap));
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//VIDIOC_CROPCAP: 카메라의 기본 캡처 영역을 확인하는 명령. 이 정보는 cropcap.defrect에 저장되고, defrect는 기본적인 캡처 영역 나타냄
    if(0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		// 카메라의 기본 캡처 영역을 v4l2_crop 구조체에 복사 (즉 카메라의 기본영역을 그대로 사용하겠다는 의미)
        crop.c = cropcap.defrect; /* reset to default */
		// 이 설정을 실제 카메라에 적용(즉 이 영역만큼 영상 자를 준비)
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
    // 2를 곱한건, 16비트컬러모드를 가정하고 하드코딩한것.
    // 보통 vinfo.bits_per_pixel / 8 로 함
    long screensize = vinfo.xres * vinfo.yres * 2;
    
    // 18비트컬러에서는 한 픽셀이 2바이트(16비트) 라서 1픽셀을 short(2바이트) 로 처리하면 더 편리.
    // unsigned char* 사용하면 1픽셀 처리할때 2번 접근해야함
    // unsigned short* 쓰는게 좀더 맞다
    fbp = (short *)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    // mmap은 void* 형을 반환하는데, 이를 일관된타입으로 유지해주려고 명시적캐스팅
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
