#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// usage: ./sr04_test /dev/100ask_sr04
int main(int argc, char *argv[])
{
    int fd;
    int buf_ns;

    if (argc != 2)
    {
        printf("Usage: %s <dev>\n", argv[0]);
        return -1;
    }
    fd = open(argv[1], O_RDWR);
    if (fd == -1)
    {
        printf("failed to open %s\n", argv[1]);
        return -1;
    }
    while (1)
    {
        int ret;
        int distance_mm = 0;

        ret = sizeof(int);
        if (read(fd, &buf_ns, sizeof(int)) == ret)
        {
            distance_mm = buf_ns * 340 / 2 / 1000000;

            printf("get time: %d ns\n", buf_ns);
            printf("get distence: %d mm\n", distance_mm);
            printf("----------------------------------------------------------\n");
        }
        else
        {
            printf("get distence: -1\n");
        }
        sleep(1); //不延时会读不出
    }
    close(fd);

    return 0;
}