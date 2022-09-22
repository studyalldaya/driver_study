#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// usage: ./ds18b20_test /dev/100ask_ds18b20
int main(int argc, char *argv[])
{
    int fd;
    unsigned char data[5];
    unsigned int integer;
    unsigned char decimal;

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

        if (read(fd, data, 5) == 5)
        {
            integer = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
            decimal = data[4];
            printf("get temperature : %d.%d \n", integer, decimal);
            printf("----------------------------------------------------------\n");
        }
        else
        {
            printf("get temperature: -1\n");
        }
        sleep(1);
    }
    close(fd);

    return 0;
}