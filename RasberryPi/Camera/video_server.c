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
#include <signal.h>

#include <asm/types.h>               /* for videodev2.h */
#include <linux/videodev2.h>

#include <arpa/inet.h> // tcp 
#include <sys/socket.h>

#define FBDEV        "/dev/fb0"      /* 프레임 버퍼를 위한 디바이스 파일 */
#define VIDEODEV    "/dev/video0"
#define WIDTH       800               /* 캡쳐받을 영상의 크기 */
#define HEIGHT      600                



#define TCP_PORT 5100 // 서버의 포트 번호

int camfd = -1;		/* 카메라의 파일 디스크립터 */

// tcp 통신 변수
int ssock;
socklen_t clen;
int n;
struct sockaddr_in servaddr, cliaddr;
pid_t pid;
char msg[BUFSIZ];

int send_data = 0; // 카메라 데이터 전송 플래그

/* Video4Linux에서 사용할 영상 저장을 위한 버퍼 */
struct buffer {
    void* start;
    size_t length;
};

static short* fbp = NULL;         /* 프레임버퍼의 MMAP를 위한 변수 */
struct buffer* buffers = NULL;
static unsigned int n_buffers = 0;
static struct fb_var_screeninfo vinfo;                   /* 프레임버퍼의 정보 저장을 위한 구조체 */

static int csock; // 클라이언트 소켓

//int one_flag = 1; 나중에 한번만보내는거 테스트하고싶을때 쓰기

static void setFlag(int signo) {
    if (signo == SIGUSR1)
        send_data = 1;
    if (signo == SIGUSR2)
        send_data = 0;
}

static void mesg_exit(const char* s)
{
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

static int xioctl(int fd, int request, void* arg)
{
    int r;
    do r = ioctl(fd, request, arg); while (-1 == r && EINTR == errno);
    return r;
}

extern inline int clip(int value, int min, int max)
{
    return(value > max ? max : value < min ? min : value);
}

void send_camera_data(int client_socket, const void* image_data, size_t image_size) {
    // 데이터를 클라이언트로 전송
    ssize_t total_sent = 0;
    while (total_sent < image_size) {
        // image_data + total_sent 가 데이터의 시작주소 나타냄(buf역할)
        // image_size - total_sent 는 보내야 할 남은 데이터의 양
        // sent는 전송에 성공한 바이트수를 반환함. 데이터가 모두 전송될때까지 send를 호출
        ssize_t sent = send(client_socket, image_data + total_sent, image_size - total_sent, 0);
        if (sent == -1) {
            perror("send failed");
            close(client_socket);
            return;
        }
        total_sent += sent;

        usleep(20000);
    }
}

static int read_frame(int fd)
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
        switch (errno) {
        case EAGAIN: return 0;
        case EIO:
            /* Could ignore EIO, see spec. */
        default: mesg_exit("VIDIOC_DQBUF");
        }
    }

    if (send_data) {
        // 카메라에서 얻은 데이터를 클라이언트에게 전송
        size_t image_size = buffers[buf.index].length;
        send_camera_data(csock, buffers[buf.index].start, image_size);
    }

    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        mesg_exit("VIDIOC_QBUF");

    return 1;
}

static void mainloop(int fd)
{
    fd_set fds;
    struct timeval tv;
    int r;

    while (1) {
        // 카메라 데이터 계속 읽기 및 갱신
        while (1) {
            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);
            if (-1 == r) {
                if (EINTR == errno) continue;
                mesg_exit("select");
            }
            else if (0 == r) {
                fprintf(stderr, "select timeout\n");
                continue;
            }

            // 카메라 데이터 읽기 및 처리
            if (read_frame(fd)) {
                usleep(50000); // 50ms대기 (전송속도 조절)
                break;
            }
        }
    }
}

static void start_capturing(int fd)
{
    for (int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            mesg_exit("VIDIOC_QBUF");
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // VIDIOC_STREAMON : V4L2장치에서 스트리밍을 시작하는 명령
    // 이 명령 호출시 장치가 지속적으로 프레임을 캡쳐하여 VIDIOC_QBUF 으로 큐에 있는 버퍼에 데이터저장
    // 이후 VIDIOC_DQBUF 시 큐에서 사용가능한 버퍼 꺼내 프레임데이터 처리가능
    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
        mesg_exit("VIDIOC_STREAMON");
}

static void init_mmap(int fd)
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", VIDEODEV);
            exit(EXIT_FAILURE);
        }
        else {
            mesg_exit("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", VIDEODEV);
        exit(EXIT_FAILURE);
    }

    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;
        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
            mesg_exit("VIDIOC_QUERYBUF");

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
            MAP_SHARED, fd, buf.m.offset);
        if (MAP_FAILED == buffers[n_buffers].start)
            mesg_exit("mmap");
    }
}

static void init_device(int fd)
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", VIDEODEV);
            exit(EXIT_FAILURE);
        }
        else {
            mesg_exit("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n",
            VIDEODEV);
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", VIDEODEV);
        exit(EXIT_FAILURE);
    }

    /* Select video input, video standard and tune here. */
    memset(&cropcap, 0, sizeof(cropcap));
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */
        xioctl(fd, VIDIOC_S_CROP, &crop);
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
        mesg_exit("VIDIOC_S_FMT");

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
        fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
        fmt.fmt.pix.sizeimage = min;

    init_mmap(fd);
}

static int set_camera() {
    /* 카메라 장치 열기 */
    camfd = open(VIDEODEV, O_RDWR | O_NONBLOCK, 0);
    if (-1 == camfd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n",
            VIDEODEV, errno, strerror(errno));
        return EXIT_FAILURE;
    }

    init_device(camfd);
    start_capturing(camfd);

    return 1;
}

static int open_server() {
    // 서버 소켓 열기
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);

    if (bind(ssock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind()");
        return -1;
    }
    if (listen(ssock, 8) < 0) {
        perror("listen()");
        return -1;
    }
    return 1;
}

int main(int argc, char** argv)
{
    if (set_camera() != 1) return 0;
    if (open_server() != 1) return 0;

    clen = sizeof(cliaddr);
    while (1) {
        csock = accept(ssock, (struct sockaddr*)&cliaddr, &clen);

        if ((pid = fork()) < 0) {
            perror("fork()");
        }
        else if (pid == 0) {
            while (1) {
                memset(msg, 0, BUFSIZ);
                if ((n = read(csock, msg, BUFSIZ)) <= 0) {
                    perror("read()");
                    close(csock);
                    break;
                }
                // 클라이언트 요청에 따라 전송 플래그 설정
                if (!strcmp(msg, "1")) {
                    kill(getppid(), SIGUSR1);
                }
                else if (!strcmp(msg, "2")) {
                    kill(getppid(), SIGUSR2);
                }
            }
        }
        else {
            signal(SIGUSR1, setFlag);
            signal(SIGUSR2, setFlag);
            mainloop(camfd);
        }
    }

    /* 캡쳐 중단 */
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(camfd, VIDIOC_STREAMOFF, &type))
        mesg_exit("VIDIOC_STREAMOFF");

    /* 메모리 정리 */
    for (int i = 0; i < n_buffers; ++i)
        if (-1 == munmap(buffers[i].start, buffers[i].length))
            mesg_exit("munmap");
    free(buffers);

    /* 장치 닫기 */
    if (-1 == close(camfd))
        mesg_exit("close");

    return EXIT_SUCCESS;
}
