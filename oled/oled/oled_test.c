#if 1
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "font.h"

// ./oled_test /dev/100ask_oled

#define OLED_SET_XY 99
#define OLED_SET_XY_WRITE_DATA 100  // IO()
#define OLED_SET_XY_WRITE_DATAS 101 // IO()
#define OLED_SET_DATAS 102          //低8位表示ioctl 高位表示数据长度

int fd_oled;

void OLED_DIsp_Char(int x, int y, unsigned char c)
{
    int i = 0;
    char pos[2];

    /* 得到字模 */
    const unsigned char *dots = oled_asc2_8x16[c - ' '];
#if 0
    /* 发给OLED */
    OLED_DIsp_Set_Pos(x, y);
    /* 发出8字节数据 */
    for (i = 0; i < 8; i++)
        oled_write_cmd_data(dots[i], OLED_DATA);
#endif
    pos[0] = x;
    pos[1] = y;
    ioctl(fd_oled, OLED_SET_XY, pos);
    ioctl(fd_oled, OLED_SET_DATAS | (8 << 8), dots);
#if 0
    OLED_DIsp_Set_Pos(x, y + 1);
    /* 发出8字节数据 */
    for (i = 0; i < 8; i++)
        oled_write_cmd_data(dots[i + 8], OLED_DATA);
#endif
    pos[0] = x;
    pos[1] = y + 1;
    ioctl(fd_oled, OLED_SET_XY, pos);
    ioctl(fd_oled, OLED_SET_DATAS | (8 << 8), &dots[8]);
}

void OLED_DIsp_String(int x, int y, char *str)
{
    unsigned char j = 0;
    while (str[j])
    {
        OLED_DIsp_Char(x, y, str[j]); //显示单个字符
        x += 8;
        if (x > 127)
        {
            x = 0;
            y += 2;
        } //移动显示位置
        j++;
    }
}

void OLED_DIsp_CHinese(unsigned char x, unsigned char y, unsigned char no)
{
    unsigned char t, adder = 0;
    char pos[2];
    pos[0] = x;
    pos[1] = y;
    ioctl(fd_oled, OLED_SET_XY, pos);

    for (t = 0; t < 16; t++)
    { //显示上半截字符
        // oled_write_cmd_data(hz_1616[no][t * 2], OLED_DATA);
        ioctl(fd_oled, OLED_SET_DATAS | (1 << 8), &hz_1616[no][t * 2]);
        adder += 1;
    }
    pos[0] = x;
    pos[1] = y + 1;
    ioctl(fd_oled, OLED_SET_XY, pos);
    // OLED_DIsp_Set_Pos(x, y + 1);
    for (t = 0; t < 16; t++)
    { //显示下半截字符
        // oled_write_cmd_data(hz_1616[no][t * 2 + 1], OLED_DATA);
        ioctl(fd_oled, OLED_SET_DATAS | (1 << 8), &hz_1616[no][t * 2 + 1]);
        adder += 1;
    }
}

void OLED_DIsp_Test(void)
{
    int i;
    OLED_DIsp_String(0, 0, "yo! it's a good day!");
#if 0
    OLED_DIsp_String(0, 0, "wiki.100ask.net");
    OLED_DIsp_String(0, 2, "book.100ask.net");
    OLED_DIsp_String(0, 4, "bbs.100ask.net");
#endif
#if 0
    for (i = 0; i < 3; i++)
    { //显示汉字 百问网
        OLED_DIsp_CHinese(32 + i * 16, 6, i);
    }
#endif
}

int main(int argc, char *argv[])
{

    int buf[2];

    if (argc != 2)
    {
        printf("Usage : %s <dev> \n", argv[0]);

        return -1;
    }

    fd_oled = open(argv[1], O_RDWR);
    if (fd_oled < 0)
    {
        printf(" can not open dev %s\n", argv[0]);
        return -1;
    }

    OLED_DIsp_Test();

    return 0;
}
#endif

#if 0
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "font.h"

// ./oled_test

#define OLED_SET_XY 99
#define OLED_SET_XY_WRITE_DATA 100  // IO()
#define OLED_SET_XY_WRITE_DATAS 101 // IO()
#define OLED_SET_DATAS 102          //低8位表示ioctl 高位表示数据长度

int fd_oled;
int fd_sr04;
char dis_buf[10];

void OLED_DIsp_Char(int x, int y, unsigned char c)
{
    int i = 0;
    char pos[2];

    /* 得到字模 */
    const unsigned char *dots = oled_asc2_8x16[c - ' '];
    pos[0] = x;
    pos[1] = y;
    ioctl(fd_oled, OLED_SET_XY, pos);
    ioctl(fd_oled, OLED_SET_DATAS | (8 << 8), dots);

    pos[0] = x;
    pos[1] = y + 1;
    ioctl(fd_oled, OLED_SET_XY, pos);
    ioctl(fd_oled, OLED_SET_DATAS | (8 << 8), &dots[8]);
}

void OLED_DIsp_String(int x, int y, char *str)
{
    unsigned char j = 0;
    while (str[j])
    {
        OLED_DIsp_Char(x, y, str[j]); //显示单个字符
        x += 8;
        if (x > 127)
        {
            x = 0;
            y += 2;
        } //移动显示位置
        j++;
    }
}

int get_distance()
{
    int ret;
    int dis_mm;
    int buf_ns;
    ret = sizeof(int);

    if (read(fd_sr04, &buf_ns, sizeof(int)) == ret)
    {
        dis_mm = buf_ns * 340 / 2 / 1000000;
    }
    else
    {
        printf("get distence: -1\n");
    }
    return dis_mm;
}

int main(int argc, char *argv[])
{

    int buf[2];
    int dis_mm;

    fd_oled = open("/dev/100ask_oled", O_RDWR);
    if (fd_oled < 0)
    {
        printf(" can not open dev 100ask_oled\n");
        return -1;
    }
    // fd_sr04 = open("/dev/100ask_sr04", O_RDWR);
    // if (fd_sr04 < 0)
    // {
    //     printf(" can not open dev 100ask_sr04\n");
    //     return -1;
    // }
    OLED_DIsp_String(0, 0, "welcome!");
#if 0
    while (1)
    {
        dis_mm = get_distance();

        dis_buf[9] = dis_mm % 10;
        dis_buf[8] = dis_mm / 10 % 10;
        dis_buf[7] = dis_mm / 100 % 10;
        dis_buf[6] = dis_mm / 1000 % 10;
        dis_buf[5] = dis_mm / 10000 % 10;

        OLED_DIsp_String(2, 2, dis_buf);
    }
#endif
    return 0;
}
#endif