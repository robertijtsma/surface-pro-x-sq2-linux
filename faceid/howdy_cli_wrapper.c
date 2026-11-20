#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	(void)argc;
	setenv("OMP_NUM_THREADS", "1", 1);
	setenv("OPENCV_FOR_THREADS_NUM", "1", 1);
	setenv("OPENBLAS_NUM_THREADS", "1", 1);
	setenv("OPENCV_VIDEOIO_PRIORITY_GSTREAMER", "0", 1);
	setenv("OPENCV_LOG_LEVEL", "ERROR", 1);
	execv("/usr/bin/howdy.bin", argv);
	return 127;
}
