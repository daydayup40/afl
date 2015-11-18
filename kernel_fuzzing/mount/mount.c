#include <sys/mount.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <linux/loop.h>

#include "forkserver.h"

#include "btrfs.c"

#define die(...) do { \
    fprintf(stderr, __VA_ARGS__);		\
    abort();					\
  } while (0)

static char mount_point[16];
static void unmount_it(int mount_nr)
{
  snprintf(mount_point, sizeof(mount_point), "/mnt/%d", mount_nr);
  umount(mount_point);
}

static int loop_fd;
static char loop_device[16];
static void loop_setup(int loop_nr)
{
  snprintf(loop_device, sizeof(loop_device), "/dev/loop%d", loop_nr);

  loop_fd = open(loop_device, O_RDWR);
  if (loop_fd < 0)
    die("Could not open loop device %s [%s]\n", loop_device, strerror(errno));
}

static void loop_detach(void)
{
  ioctl(loop_fd, LOOP_CLR_FD, 0);
}

static void loop_attach(const char* file)
{
  unsigned int max_nr_retry = 42;
  int file_fd = open(file, O_RDWR);
  if (file_fd < 0)
    die("Could not open file to attach %s [%s]\n", file, strerror(errno));
 retry:
  if (ioctl(loop_fd, LOOP_SET_FD, file_fd)) {
    if (errno == EBUSY && --max_nr_retry)
      goto retry;
    die("Could not configure loop device %s with %s [%s]\n", loop_device, file, strerror(errno));
  }
  close(file_fd);
}

static void loop_setinfo(const char* file)
{
  static struct loop_info64 linfo;
  strncpy(linfo.lo_file_name, file, sizeof(linfo.lo_file_name));
  ioctl(loop_fd, LOOP_SET_STATUS64, &linfo);
}

static const char* fstype;
static void mount_it()
{
  if (mount(loop_device, mount_point, fstype, 0x0, NULL) == -1 &&
      errno == EACCES) {
    errno = 0;
    mount(loop_device, mount_point, fstype, MS_RDONLY, NULL);
  }
	DIR *dir = opendir(mount_point);
	if (dir) {
		readdir(dir);
		closedir(dir);
	}

}

static int nr_fuzzer;
void load_hook(unsigned int argc, char**argv)
{
  nr_fuzzer = atoi(argv[2]);
  fstype = argv[1];
  loop_setup(nr_fuzzer);
}

int run(unsigned int argc, char** argv)
{
  if (argc < 4) {
    fprintf(stderr, "Missing input file to mount.\n");
    return -1;
  }

  unmount_it(nr_fuzzer);

	static char temp_filename[64];
	sprintf(temp_filename, "%s.%u", argv[3], nr_fuzzer);
	construct_image(argv[3], temp_filename);

  loop_detach();
  loop_attach(temp_filename);
  loop_setinfo(temp_filename);

  mount_it();

  return 0;
}