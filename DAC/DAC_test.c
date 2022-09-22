
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// ./DAC_test /dev/100ask_DAC

int main(int argc, char *argv[])
{
    int fd;
    unsigned short dac_val = 0;

    if (argc != 2)
    {
        printf("Usage : %s <dev> \n", argv[0]);

        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0)
    {
        printf(" can not open dev %s\n", argv[0]);
        return -1;
    }

    while (1)
    {
        write(fd, &dac_val, 2);
        dac_val++;
    }
    return 0;
}
