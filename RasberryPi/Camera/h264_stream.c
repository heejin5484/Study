#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>  // for ioctl
#include <unistd.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define WIDTH 800
#define HEIGHT 600
#define FBDEV "/dev/fb0"

// 프레임버퍼에 쓸 메모리 매핑 및 설정 함수
int init_framebuffer(int *fbfd, struct fb_var_screeninfo *vinfo, struct fb_fix_screeninfo *finfo, char **fbp) {
    *fbfd = open(FBDEV, O_RDWR);
    if (*fbfd == -1) {
        perror("Error opening framebuffer device");
        return -1;
    }

    if (ioctl(*fbfd, FBIOGET_VSCREENINFO, vinfo)) {
        perror("Error reading variable information");
        return -1;
    }

    if (ioctl(*fbfd, FBIOGET_FSCREENINFO, finfo)) {
        perror("Error reading fixed information");
        return -1;
    }

    *fbp = (char *)mmap(0, finfo->smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, *fbfd, 0);
    if ((long)*fbp == -1) {
        perror("Error: failed to map framebuffer device to memory");
        return -1;
    }

    return 0;
}

// YUV420p → RGB565 변환 후 프레임버퍼에 출력
void yuv420p_to_rgb565(AVFrame *frame, struct SwsContext *sws_ctx, struct fb_var_screeninfo *vinfo, char *fbp, int width, int height) {
    // RGB565 프레임을 위한 출력 버퍼
    uint8_t *rgb_frame[1];
    rgb_frame[0] = (uint8_t *)malloc(width * height * 2); // RGB565는 2 bytes per pixel

    int rgb_stride[1] = { width * 2 };  // RGB565는 2바이트 픽셀

    // 색상 공간 변환: YUV420p → RGB565
    sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, height, rgb_frame, rgb_stride);

    // 프레임버퍼에 데이터 복사
    for (int y = 0; y < height; y++) {
        memcpy(fbp + y * vinfo->xres * 2, rgb_frame[0] + y * width * 2, width * 2);
    }

    free(rgb_frame[0]);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return -1;
    }

    const char *filename = argv[1];
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVFrame *frame = NULL;
    AVPacket *packet = av_packet_alloc();
    struct SwsContext *sws_ctx = NULL;
    int video_stream_idx = -1;
    int fbfd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    char *fbp;

    // 프레임버퍼 초기화
    if (init_framebuffer(&fbfd, &vinfo, &finfo, &fbp) < 0) {
        return -1;
    }

    // FFmpeg 초기화
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open file %s\n", filename);
        return -1;
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not retrieve stream info from file\n");
        return -1;
    }

    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {
        fprintf(stderr, "No video stream found\n");
        return -1;
    }
    // ffmpeg 라이브러리 사용해서 비디오 코덱 찾는 과정임
    //avcodec_find_decoder 함수는 특정한 코덱 ID 기반으로 해당 코덱 찾는 역할 함
    // fmt_ctx는 비디오 파일의 전체적인 정보 담고있음
    // codecpar는 AVCodecParameters 구조체를 가리킴. 이 구조체는 해당스트림에서 사용된 코덱에 대한 파라미터 담음
    // codec_id 는 해당 비디오스트림이 사용중인 코덱 찾음 
    codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_idx]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        return -1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Could not allocate codec context\n");
        return -1;
    }

    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    frame = av_frame_alloc();

    // 색상 공간 변환을 위한 SwsContext 초기화
    sws_ctx = sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_YUV420P, WIDTH, HEIGHT, AV_PIX_FMT_RGB565LE, SWS_BILINEAR, NULL, NULL, NULL);

    // av_read_frame는 비디오파일에서 다음패킷을 읽어옴. 패킷은 압축된 비디오 데이터
    // 패킷을 packet 구조체에 저장 후, 이후 디코딩에 사용
    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_idx) {
            // avcodec_send_packet은 압축된 비디오 데이터를 디코더에 보내는 역할
            // 즉 packet을 codec_ctx로 보내서 디코더가 해당 패킷 처리하게 함
            // 패킷은 압축된 데이터를 포함하고 있는데, 이 데이터는 AVCodecContext에 의해 디코딩됨
            if (avcodec_send_packet(codec_ctx, packet) >= 0) {
                // avcodec_receive_frame은 압축된 데이터 풀어서 frame에 저장함
                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                    // YUV420p를 RGB565로 변환하여 프레임버퍼에 출력
                    yuv420p_to_rgb565(frame, sws_ctx, &vinfo, fbp, WIDTH, HEIGHT);
                    usleep(40000);  // 40ms 딜레이 (약 25fps) ->> 이게 프레임수가 달라지면 하드코딩하면 안되니까 1초 나누기 codec_ctx->framerate.num 만큼 하면 좀더 좋은 코드로 만들수있음
                    //즉 1초에 몇프레임 표시할거냐 이 정보 가지구 그만큼 sleep을 걸어야 우다다다 출력되지 않음
                }
            }
        }
        av_packet_unref(packet);
    }

    // 리소스 정리
    munmap(fbp, finfo.smem_len);
    close(fbfd);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);

    return 0;
}
