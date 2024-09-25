#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <linux/fb.h> // fb_var_screeninfo 구조체
#include <stdlib.h>
#include <fcntl.h> // open
#include <sys/mman.h>  // munmap(mmap 맵핑영역 해제시 사용)
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>

#define TCP_PORT 5100
#define SERVER_IP "127.0.0.1"

#define WIDTH 800
#define HEIGHT 600

#define FBDEV "/dev/fb0" /* 프레임 버퍼를 위한 디바이스 파일 */

static struct fb_var_screeninfo vinfo; /* 프레임버퍼의 정보 저장을 위한 구조체 */
long screensize;
int fbfd = -1; /* 프레임버퍼의 파일 디스크립터 */
static short* fbp = NULL; /* 프레임버퍼의 MMAP를 위한 변수 */

char buffer[WIDTH * HEIGHT * 2] = { 0 }; // 프레임 버퍼 크기

int sock;
struct sockaddr_in servaddr;
char msg[BUFSIZ];
int pid;

static void mesg_exit(const char* s)
{
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

/* unsigned char의 범위를 넘어가지 않도록 경계 검사를 수행다. */
extern inline int clip(int value, int min, int max)
{
    return(value > max ? max : value < min ? min : value);
}

static void process_image(const void* p)
{
    unsigned char* in = (unsigned char*)p;
    int width = WIDTH;
    int height = HEIGHT;
    int istride = WIDTH * 2; /* 이미지의 폭을 넘어가면 다음 라인으로 내려가도록 설정 */
    int x, y, j;
    int y0, u, y1, v, r, g, b;
    unsigned short pixel;
    long location = 0;
    for (y = 0; y < height; ++y) {
        for (j = 0, x = 0; j < vinfo.xres * 2; j += 4, x += 2) {
            if (j >= width * 2) { /* 현재의 화면에서 이미지를 넘어서는 빈 공간을 처리 */
                location++; location++;
                continue;
            }
            /* YUYV 성분을 분리 */
            y0 = in[j];
            u = in[j + 1] - 128;
            y1 = in[j + 2];
            v = in[j + 3] - 128;

            /* YUV를 RGB로 전환 */
            r = clip((298 * y0 + 409 * v + 128) >> 8, 0, 255);
            g = clip((298 * y0 - 100 * u - 208 * v + 128) >> 8, 0, 255);
            b = clip((298 * y0 + 516 * u + 128) >> 8, 0, 255);
            pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); /* 16비트 컬러로 전환 */
            fbp[location++] = pixel;

            /* YUV를 RGB로 전환 */
            r = clip((298 * y1 + 409 * v + 128) >> 8, 0, 255);
            g = clip((298 * y1 - 100 * u - 208 * v + 128) >> 8, 0, 255);
            b = clip((298 * y1 + 516 * u + 128) >> 8, 0, 255);
            pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); /* 16비트 컬러로 전환 */
            fbp[location++] = pixel;
        };
        in += istride;
    };
}

static int set_framebuffer()
{
    /* 프레임버퍼 열기 */
    fbfd = open(FBDEV, O_RDWR);
    if (-1 == fbfd) {
        perror("open( ) : framebuffer device");
        return EXIT_FAILURE;
    }

    /* 프레임버퍼의 정보 가져오기 */
    if (-1 == ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information.");
        return EXIT_FAILURE;
    }

    /* mmap( ) : 프레임버퍼를 위한 메모리 공간 확보 */
    screensize = vinfo.xres * vinfo.yres * 2;
    fbp = (short*)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == (short*)-1) {
        perror("mmap() : framebuffer device to memory");
        return EXIT_FAILURE;
    }

    memset(fbp, 0, screensize);

    return 1;
}

static int connect_server(char* server_ip)
{
    // 소켓 생성
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(TCP_PORT);

    // 서버 주소 설정
    if (inet_pton(AF_INET, server_ip, &(servaddr.sin_addr.s_addr)) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 서버에 연결
    if (connect(sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }
    return 1;
}

static int stream_start()
{
    if ((pid = fork()) < 0) {
        perror("[ERROR] : fork()");
        return 0;
    }
    else if (pid == 0) { // 자식프로세스 처리 (데이터 수신/프레임버퍼에 그리는 역할)
        // 자식 프로세스는 프레임버퍼 접근만 처리
        while (1) {
            // 데이터 수신
            ssize_t total_received = 0;
            ssize_t frame_size = WIDTH * HEIGHT * 2;

            while (total_received < frame_size) {
                ssize_t received = recv(sock, buffer + total_received, frame_size - total_received, 0);
                if (received == -1) {
                    perror("recv failed");
                    close(sock);
                    return -1;
                }
                total_received += received;
            }
            // 받은 데이터를 처리 (프레임버퍼에 출력)
            process_image(buffer);  // 수신한 데이터를 프레임버퍼에 출력
        }
    }
    else {
        while (1) {
            memset(msg, 0, BUFSIZ);
            printf("enter 1 to stream, enter 2 to quit.\n");
            scanf("%s", msg);
            if (!strcmp(msg, "2")) {
                kill(pid, SIGKILL);
                break;
            }
        }
    }
    return 1;
}

int main()
{
    // 프레임버퍼 설정
    set_framebuffer();
    // 서버 연결
    if (connect_server(SERVER_IP) != 1) {
        return 0;
    }

    while (1) {
        memset(msg, 0, BUFSIZ);
        printf("enter 1 to stream, enter 2 to quit.\n");
        scanf("%s", msg);
        if (!strcmp(msg, "1")) {
            send(sock, msg, strlen(msg), MSG_DONTWAIT);
            stream_start();
        }
        else if (!strcmp(msg, "2"))
            break;
    }

    // 종료 시 프레임버퍼 정리
    munmap(fbp, screensize);

    /* 장치 닫기 */
    if (-1 == close(fbfd))
        mesg_exit("close");

    return EXIT_SUCCESS;
}
