#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static int fd;

/*./motor  /dev/100ask_motor  4096(360度)  10(速度1~10)     */
int main(int argc, char **argv)
{
    int buf[2];
    int ret;

    if (argc != 4)
    {
        printf("Usage: %s <dev> <steps> <speed 1~10>\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1)
    {
        printf("can not open file %s\n", argv[1]);
        return -1;
    }

    // str --> int
    buf[0] = strtol(argv[2], NULL, 0);
    buf[1] = strtol(argv[3], NULL, 0);
    if (buf[1] > 10 || buf[0] < 1)
    {
        printf("err: speed must be 1~10,now it is %d \n", buf[1]);
        close(fd);
        return -1;
    }

    ret = write(fd, buf, 8);

    close(fd);

    return 0;
}