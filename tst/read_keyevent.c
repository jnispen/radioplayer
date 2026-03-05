#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>

int main(int argc, char* argv[])
{
    struct input_event ev[64];
    int fevdev = -1;
    int result = 0;
    int size = sizeof(struct input_event);
    int rd;
    char name[256] = "Unknown";
    char *device; // e.g. "/dev/input/event0"

    if (argc < 2) {
        printf ("Usage: %s <input_device>\n", argv[0]);
        return 1;
    }
    device = argv[1];

    fevdev = open(device, O_RDONLY);
    if (fevdev == -1) {
        printf("Failed to open event device.\n");
        exit(1);
    }

    if (ioctl(fevdev, EVIOCGNAME(sizeof(name)), name) < 0) {
        perror("ioctl EVIOCGNAME");
		exit(1);
    }

    printf ("Reading From : %s (%s)\n", device, name);

    printf("Getting exclusive access: ");
    result = ioctl(fevdev, EVIOCGRAB, 1);
    printf("%s\n", (result == 0) ? "SUCCESS" : "FAILURE");

    int count = 0;
    while (1)
    {
        rd = read(fevdev, ev, size * 64);
    
        if (rd < 0) {
            perror("read");
            break;
        }
        
        if (rd == 0) {
            printf("End of input.\n");
            break;
        }
        
        if (rd < size) {
            printf("Warning: incomplete event data\n");
            continue;
        }

        count = rd / size;
        for (int i = 0; i < count; i++) {
            if (ev[i].type == 1 && ev[i].value == 1) {  // Key press
                printf("Key Code: [%d]\n", ev[i].code);
            }
        }
    }

    printf("Exiting.\n");
    result = ioctl(fevdev, EVIOCGRAB, 0);
    close(fevdev);
    
    return 0;
}
