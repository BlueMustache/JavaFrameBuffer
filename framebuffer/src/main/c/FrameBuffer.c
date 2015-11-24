/*
 *	This file is the JNI C part of a Raspberry Pi FrameBuffer project.
 *
 *	Created 2013 by Thomas Welsch (ttww@gmx.de).
 *
 *	Do whatever you want to do with it :-)
 *
 *	This code transfers an Java BufferedImage ARGB data array to a FrameBuffer device
 *	(e.g. SPI-Displays like http://www.sainsmart.com/blog/ada/).
 *
 *	For testing purpose a dummy device is supported (via the devicename "dummy_160x128" instead of "/dev/fb1").
 *
 **/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#ifdef __linux
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

#include <sys/mman.h>

#include <jni.h>

struct deviceInfo {
	char *deviceName;				// Device-Name from Java ("/dev/fb1" or "dummy_240x180")...
	int fbfd;						// File descriptor, 0 for dummy devices

	int width;
	int height;
	int bpp;						// BitsPerPixel, 0 for dummy devices

	long int screensize;			// Buffer size in bytes

	char *fbp;						// MemoryMapped buffer

	unsigned int *dummy;	// Last screen
};

// http://stackoverflow.com/questions/4770985/how-to-check-if-a-string-starts-with-another-string-in-c
static int starts_with(const char *pre, const char *str) {
	size_t lenpre = strlen(pre);
	size_t lenstr = strlen(str);
	return lenstr < lenpre ? 0 : strncmp(pre, str, lenpre) == 0;
}

JNIEXPORT jlong JNICALL Java_org_tw_pi_framebuffer_FrameBuffer_openDevice(
		JNIEnv *env, jobject obj, jstring device) {

	jboolean isCopy;

	struct deviceInfo *di;

	di = malloc(sizeof(*di));
	memset(di, 0, sizeof(*di));

	const char *s = (*env)->GetStringUTFChars(env, device, &isCopy);
	di->deviceName = strdup(s);
	if (isCopy)
		(*env)->ReleaseStringUTFChars(env, device, s);

	// Open the file for reading and writing
	if (!starts_with("dummy_", di->deviceName)) {

#ifndef __linux
		//		printf("Error: Framebuffer only under linux, use dummy device (dummy_220x440) instead %s\n",di->deviceName);
		return (1);
#else

		struct fb_var_screeninfo vinfo;
		struct fb_fix_screeninfo finfo;

		di->fbfd = open(di->deviceName, O_RDWR);
		if (!di->fbfd) {
			//			printf("Error: cannot open framebuffer device. %s\n", di->deviceName);
			return (1);
		}
		//		printf("The framebuffer device %s was opened successfully.\n", di->deviceName);

		// Get fixed screen information
		if (ioctl(di->fbfd, FBIOGET_FSCREENINFO, &finfo)) {
			//			printf("Error reading fixed information.\n");
			return (2);
		}

		// Get variable screen information
		if (ioctl(di->fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
			//			printf("Error reading variable information.\n");
			return (3);
		}

		di->width = vinfo.xres;
		di->height = vinfo.yres;
		di->bpp = vinfo.bits_per_pixel;
		di->dummy = NULL;

		//		printf("%dx%d, %d bpp  %ld bytes\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, (long) finfo.smem_len);

		// map framebuffer to user memory
		di->screensize = finfo.smem_len;

		di->fbp = (char*) mmap(0, di->screensize, PROT_READ | PROT_WRITE,
				MAP_SHARED, di->fbfd, 0);

		if ((int) di->fbp == -1) {
			//			printf("Failed to mmap.\n");
			return (4);
		}
#endif
	} else {
		// Parse dummy_123x343
		sscanf(di->deviceName, "dummy_%dx%d", &di->width, &di->height);
		di->bpp = 0;
		di->dummy = malloc(di->width * di->height * sizeof(int));
	}
	return (jlong) (intptr_t) di;
}

JNIEXPORT void JNICALL Java_org_tw_pi_framebuffer_FrameBuffer_closeDevice(
		JNIEnv *env, jobject obj, jlong jdi) {

	struct deviceInfo *di = (struct deviceInfo *) (intptr_t) jdi;

	free(di->deviceName);
	if(di->dummy)
		free(di->dummy);

	if (di->fbfd != 0) {
		munmap(di->fbp, di->screensize);
		close(di->fbfd);
	}

	memset(di, 0, sizeof(*di)); // :-)
}

JNIEXPORT jint JNICALL Java_org_tw_pi_framebuffer_FrameBuffer_getDeviceWidth(
		JNIEnv *env, jobject obj, jlong jdi) {

	struct deviceInfo *di = (struct deviceInfo *) (intptr_t) jdi;

	return di->width;
}

JNIEXPORT jint JNICALL Java_org_tw_pi_framebuffer_FrameBuffer_getDeviceHeight(
		JNIEnv *env, jobject obj, jlong jdi) {

	struct deviceInfo *di = (struct deviceInfo *) (intptr_t) jdi;

	return di->height;
}

JNIEXPORT jint JNICALL Java_org_tw_pi_framebuffer_FrameBuffer_getDeviceBitsPerPixel(
		JNIEnv *env, jobject obj, jlong jdi) {

	struct deviceInfo *di = (struct deviceInfo *) (intptr_t) jdi;

	return di->bpp;
}

static inline unsigned short to_16bit(unsigned int rgb) {
	unsigned char r = (rgb >> 16) & 0x0ff;
	unsigned char g = (rgb >> 8) & 0x0ff;
	unsigned char b = (rgb) & 0x0ff;

	return ((r / 8) << 11) + ((g / 4) << 5) + (b / 8);
}

static inline unsigned int from_16bit(unsigned short rgb) {
	unsigned int r = 0xff & ((rgb >> 11) << 3);
	unsigned int g = 0xff & ((rgb >> 5) << 2);
	unsigned int b = 0xff & (rgb << 2);

	return (r << 16) + (g << 8) + b;
}

JNIEXPORT jvoid JNICALL Java_org_tw_pi_framebuffer_FrameBuffer_writeDeviceBuffer(
		JNIEnv *env, jobject obj, jlong jdi, jintArray buf) {

	struct deviceInfo	*di = (struct deviceInfo *) (intptr_t) jdi;
	int					i;
	jsize				len = (*env)->GetArrayLength(env, buf);
	unsigned int		*dummy = di->dummy;


	jint			*body = (*env)->GetPrimitiveArrayCritical(env, buf, 0);
	unsigned short *p = (unsigned short *) di->fbp;

	switch (di->bpp) {
	case 0:
		// Dummy Device
		for (i = 0; i < len; i++) {
			unsigned int rgb = body[i];
			dummy[i] = rgb;
		}
		break;
	case 16:
		// Comment from:
		//		http://raspberrycompote.blogspot.de/2013/03/low-level-graphics-on-raspberry-pi-part_8.html
		//
		// The red value has 5 bits, so can be in the range 0-31, therefore divide the original 0-255
		// value by 8. It is stored in the first 5 bits, so multiply by 2048 or shift 11 bits left.
		// The green has 6 bits, so can be in the range 0-63, divide by 4, and multiply by 32 or shift
		// 5 bits left. Finally the blue has 5 bits and is stored at the last bits, so no need to move.

		for (i = 0; i < len; i++) {
			unsigned int rgb = body[i];
			p[i] = to_16bit(rgb);
		}
		break;

	default:
		// do nothing
		break;
	}

	(*env)->ReleasePrimitiveArrayCritical(env, buf, body, 0);
}

