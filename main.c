#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <locale.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <errno.h>
#include <wchar.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#ifdef OPEN_CV

#include <opencv/cv.h>
#include <opencv/highgui.h>

#endif

#define BRIGHTNESS_STEP 10
#define SAMPLE_DELAY    60*10
#define DISPLAY_DIV     3
#define DISPLAY_LINES   3
#define RETRY_TIME      10
#define CHAR_SIZE       16
#ifndef true
#define true            1
#endif
#ifndef false
#define false           0
#endif

static const char *typeface = "../NotoSansCJK-Regular.ttc";
static const char *fb_node = "/dev/fb1";
static const char *dht_node = "/dev/dht11";
static const char *dist_node = "/dev/hc-sr04";

static int ret = 0;
static int fb_fd;
static int dht_fd;
static int interrupted = false;
static size_t display_width;
static size_t display_line_len;
static size_t display_height;
static size_t display_size;
static uint8_t *fb_mem;
static FT_Library ft_lib;
static FT_Face ft_face;
static time_t timep;

typedef struct kalman_data {
    float value;
    float covariance;
} kalman_data_t;

static kalman_data_t kalman_sensor = {
        .value = 0,
        .covariance = 10.f,
};

static kalman_data_t kalman_predict = {
        .value = 0,
        .covariance = 50.f,
};

static kalman_data_t kalman_last = {
        .value = 0,
        .covariance = 0,
};

static size_t kalman(kalman_data_t sensor, kalman_data_t predict, kalman_data_t *last) {
    float p = sqrtf(last->covariance * last->covariance + predict.covariance * predict.covariance);
    float kg = p / sqrtf((p * p + sensor.covariance * sensor.covariance));
    last->value = last->value + (sensor.value - last->value) * kg;
    last->covariance = sqrtf(1.0 - kg) * p;
    return (size_t) last->value;
}

static void set_brightness(uint8_t brightness) {
    uint8_t buff[8];
    int value;
    FILE *fd = fopen("/sys/class/backlight/bl-fb1/brightness", "r+");
    if (!fd)
        return;
    if (fread(buff, sizeof(uint8_t), sizeof(buff), fd) > 0) {
        value = atoi(buff);
        //printf("read value[%d]\n", value);
        while (abs(value - brightness) > BRIGHTNESS_STEP && !interrupted) {
            //printf("set value[%d]\n", value);
            fprintf(fd, "%d", value);
            value += (value > brightness ? -1 : 1) * BRIGHTNESS_STEP;
            usleep(50 * 1000);
            fflush(fd);
        }
    }
    fprintf(fd, "%d", brightness);
    fclose(fd);
}

static void update_display() {
    int ret = 0;
    int retry = 0;
    struct tm *p;
    char src[DISPLAY_LINES][32] = {"获取失败", "获取失败"};
    uint8_t value[4] = {0};
    interrupted = false;
    set_brightness(255);
    while (retry++ < RETRY_TIME && ret <= 0 && !interrupted) {
        ret = read(dht_fd, value, sizeof(value));
        if (ret > 0) {
            time(&timep);
            p = localtime(&timep);
            sprintf(src[0], "湿度：%u.%u%%", value[0], value[1]);
            sprintf(src[1], "温度：%u.%u℃", value[2], value[3]);
            sprintf(src[2], "时间：%d月%02d日 %02d:%02d", (1 + p->tm_mon), p->tm_mday, p->tm_hour, p->tm_min);
        } else
            printf("[%d] retry %d\n", ret, retry);
    }
    printf("%s , %s , %s\n", src[0], src[1], src[2]);
    wchar_t my_char;
    wchar_t my_chars[16];
    memset(fb_mem, 0, display_size);
    for (int d = 0; d < DISPLAY_LINES; d++) {
        size_t offset = 0;
        mbstowcs(my_chars, src[d], strlen(src[d]) + 1);
        for (int i = 0; i < wcslen(my_chars); i++) {
            my_char = my_chars[i];
            ret = FT_Load_Char(ft_face, my_char, FT_LOAD_RENDER);
            if (ret) {
                perror("FT_Load_Glyph");
                break;
            }
            ssize_t ascent = ft_face->size->metrics.ascender >> 6;
            ssize_t top = ft_face->glyph->bitmap_top;
            ssize_t left = ft_face->glyph->bitmap_left;
            ssize_t line_width = ft_face->glyph->bitmap.width;
            ssize_t rows = ft_face->glyph->bitmap.rows;
            uint8_t *buffer = ft_face->glyph->bitmap.buffer;
            uint8_t *tmp;
            size_t padding = ascent - top;
            /*
            printf("  ascent = %d\n, top = %d\n, left = %d\n, w = %d\n, h = %d\n",
                   ascent, top, left, line_width, rows);
            printf("---------------------------------\n");
            //*/
            for (int row = 0; row < rows; row++) {
                tmp = fb_mem + (d * display_height / DISPLAY_DIV + row + padding) * display_line_len + offset + left;
                memmove(tmp, buffer + row * line_width, line_width);
            }
            offset += line_width + left + 4;
        }
    }

    sleep(5);
    set_brightness(0);
}

static void timer(int sig) {
    if (SIGALRM == sig) {
        update_display();
        alarm(SAMPLE_DELAY);
    }
}

static void interrupt(int sig) {
    if (SIGINT == sig) {
        interrupted = true;
    }
}

int main(int argc, char *argv[]) {
    /* setup freetype*/
    setlocale(LC_ALL, "zh_CN.UTF-8");
    ret = FT_Init_FreeType(&ft_lib);
    if (ret) {
        perror("FT_Init_FreeType");
        exit(errno);
    }
    ret = FT_New_Face(ft_lib, typeface, 0, &ft_face);
    if (ret == FT_Err_Unknown_File_Format) {
        perror("FT_Err_Unknown_File_Format");
        exit(errno);
    } else if (ret) {
        perror("FT_New_Face");
        exit(errno);
    }
    FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);
    ret = FT_Set_Pixel_Sizes(ft_face, CHAR_SIZE, 0);
    // ret = FT_Set_Char_Size(ft_face, 0, char_size*64, 300, 300);
    if (ret) {
        perror("FT_Set_Char_Size");
        exit(errno);
    }
    /* setup display */
    struct fb_var_screeninfo info;
    fb_fd = open(fb_node, O_RDWR);
    if (fb_fd < 0) {
        perror("open node failed");
        exit(errno);
    }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &info) < 0) {
        perror("ioctl get fb_var_screeninfo");
        close(fb_fd);
        exit(errno);
    }
    info.xres = info.xres_virtual;
    info.yres = info.yres_virtual;
    info.bits_per_pixel = 8;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &info) < 0) {
        perror("ioctl set fb_var_screeninfo");
        goto close_fb;
    }
    display_width = info.xres;
    display_height = info.yres;
    display_line_len = display_width * info.bits_per_pixel / 8;
    display_size = display_height * display_line_len;
    /* open display */
    if (ioctl(fb_fd, FBIOBLANK, FB_BLANK_NORMAL) < 0) {
        perror("open display failed");
        close(fb_fd);
        exit(errno);
    }
    /* mmap display */
    fb_mem = (uint8_t *) mmap(NULL, display_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("mmap failed");
        goto close_fb;
    }

    /* setup DHT11 sensor */
    dht_fd = open(dht_node, O_RDWR);
    if (dht_fd < 0) {
        perror("open dht11 faild");
        goto munmap_fb;
    }

    signal(SIGALRM, timer);
    signal(SIGINT, interrupt);
    alarm(1);
    uint32_t value, real_value;
    while (!interrupted) {
        int fd = open(dist_node, O_RDWR);
        if (fd > 0) {
            if (read(fd, &value, sizeof(uint32_t)) > 0) {
                kalman_sensor.value = value;
                kalman_predict.value = kalman_last.value;
                real_value = kalman(kalman_sensor, kalman_predict, &kalman_last);
                //printf("read sensor[%u], predict[%f], real[%u]\n", value, kalman_predict.value, real_value);
                if (real_value < 300)
                    set_brightness(255);
                else
                    set_brightness(0);
            }
            close(fd);
        }
        sleep(5);
    }

    /* deinit */
    munmap(fb_mem, display_size);
    close(fb_fd);
    close(dht_fd);
    return 0;

    /* failure */
    munmap_fb:
    munmap(fb_mem, display_size);
    close_fb:
    close(fb_fd);
    exit(errno);
}
