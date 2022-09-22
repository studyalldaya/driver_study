#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// usage: ./hs0038_test /dev/100ask_hs0038
int main(int argc, char *argv[])
{
    int fd;
    unsigned char data[4];

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

        if (read(fd, data, 4) == 4)
        {
            printf("get humidity : %d.%d \n", data[0], data[1]);
            printf("get temperature : %d.%d \n", data[2], data[3]);
            printf("----------------------------------------------------------\n");
        }
        else
        {
            printf("get humidity/temperature: -1\n");
        }
        sleep(3);
    }
    close(fd);

    return 0;
}