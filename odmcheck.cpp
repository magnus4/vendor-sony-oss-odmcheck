/*
 * Copyright (C) 2017 Sony Mobile Communications Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names
 *    of its contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <cutils/klog.h>
#include <cutils/properties.h>
#include <minui/minui.h>

// Remove the line below for shutdown at mismatch
#define ODMCHECK_WARN_ONLY

#define LOGV(x...) do { KLOG_DEBUG("odmcheck", x); } while (0)
#define LOGE(x...) do { KLOG_ERROR("odmcheck", x); } while (0)
#define LOGW(x...) do { KLOG_WARNING("odmcheck", x); } while (0)

static const char *PROC_VERSION = "/proc/version";
static const char *VERSION_FILE = "/odm/odm_version.prop";
static const char *ODM_DIR = "/odm", *ROOT_DIR = "/";

static const char *TAG_KERNEL_VERSION = "ro.kernel.version";
static const char *TAG_ANDROID_VERSION = "ro.build.version";
static const char *TAG_ODM_REVISION = "ro.vendor.version";
static const char *TAG_PLATFORM_VERSION = "ro.platform.version";
static const char *BUILD_PROP_ANDROID_VERSION = "ro.build.version.release";
static const char *BUILD_PROP_ODM_VERSION = "ro.vendor.version";
static const char *BUILD_PROP_PLATFORM_VERSION = "ro.board.platform";

static const char *SYS_PROP_POWERCTL = "sys.powerctl";
static const char *SYS_PROP_POWERCTL_SHUTDOWN = "shutdown";

static int char_height, char_width;

struct odmcheck_version_info {
	char android_version[PROPERTY_VALUE_MAX];
	char kernel_version[PROPERTY_VALUE_MAX];
	char odm_revision[PROPERTY_VALUE_MAX];
	char platform_version[PROPERTY_VALUE_MAX];
};

#define BACKLIGHT_PATH         "/sys/class/leds/lcd-backlight/brightness"
#define BACKLIGHT_ON_LEVEL     100

static void odmcheck_set_backlight(bool enable)
{
	int fd;
	char buffer[10];

	if (access(BACKLIGHT_PATH, R_OK | W_OK) != 0) {
		LOGW("Backlight control not supported\n");
		return;
	}

	memset(buffer, '\0', sizeof(buffer));
	fd = open(BACKLIGHT_PATH, O_RDWR);
	if (fd < 0) {
		LOGW("Could not open backlight node : %s\n", strerror(errno));
		goto cleanup;
	}
	if (enable)
		LOGV("Enabling backlight\n");
	else
		LOGV("Disabling backlight\n");
	snprintf(buffer, sizeof(buffer), "%d\n", enable ? BACKLIGHT_ON_LEVEL : 0);
	if (write(fd, buffer,strlen(buffer)) < 0) {
		LOGW("Could not write to backlight node : %s\n", strerror(errno));
		goto cleanup;
	}
	cleanup:
	if (fd >= 0)
		close(fd);
}

int odmcheck_dir_mounted(const char *dir) {
	struct stat mp, mp_root;
	return !stat(dir, &mp) && !stat(ROOT_DIR, &mp_root) && mp.st_dev != mp_root.st_dev;
}

char *odmcheck_strip_both(char *s) {
	while (isspace(*s) && *s)
		s++;
	if (!*s) return 0;
	char *p = s;
	while (!isspace(*p) && *p)
		p++;
	*p = 0;
	return s;
}

int odmcheck_read_version_file(const char *file_path, struct odmcheck_version_info *info) {
	char line[256];
	FILE *f;
	if (!file_path || !info)
		return -1;

	f = fopen(file_path, "r");

	if (!f) {
		LOGW("Failed to open version prop file: %s\n", file_path);
		return -1;
	}
	while (fgets(line, sizeof(line), f)) {
		char *name, *value;
		if (!(value = strchr(line, '=')))
			continue;
		*value++ = 0;
		name = odmcheck_strip_both(line);
		value = odmcheck_strip_both(value);
		if (!name || !value)
			continue;
		if (strcmp(TAG_ANDROID_VERSION, name) == 0) {
			strncpy(info->android_version, value, sizeof(info->android_version));
			LOGV("Version: %s\n", info->android_version);
		} else if (strcmp(TAG_ODM_REVISION, name) == 0) {
			LOGV("Revision: %s\n", value);
			strncpy(info->odm_revision, value, sizeof(info->odm_revision));
		} else if (strcmp(TAG_KERNEL_VERSION, name) == 0) {
			strncpy(info->kernel_version, value, sizeof(info->android_version));
			LOGV("Kernel: %s\n", info->kernel_version);
		} else if (strcmp(TAG_PLATFORM_VERSION, name) == 0) {
			strncpy(info->platform_version, value, sizeof(info->platform_version));
			LOGV("Platform: %s\n", info->platform_version);
		}
	}
	fclose(f);
	return 0;
}

static int odmcheck_get_kernel_version(char *kernel_version) {
	int ret = -1, major, minor, micro;
	char line[256];
	FILE *f;
	if (!kernel_version)
		return -1;

	f = fopen(PROC_VERSION, "r");

	if (!f) {
		LOGE("Failed to open %s\n", PROC_VERSION);
		return -1;
	}
	if (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "Linux version %d.%d.%d-", &major, &minor, &micro) == 3) {
			LOGV("Parsed version = %d.%d.%d\n", major, minor, micro);
			sprintf(kernel_version, "%d.%d", major, minor);
			ret = 0;
		}
	}
	fclose(f);
	return ret;
}

static int odmcheck_read_build_prop(struct odmcheck_version_info *build_info) {
	int ret = 0;
	if (!build_info)
		return -1;
	if (property_get(BUILD_PROP_ANDROID_VERSION, (char*)&build_info->android_version, NULL) <= 0 ||
	    odmcheck_get_kernel_version((char*)&build_info->kernel_version) ||
	    property_get(BUILD_PROP_ODM_VERSION, (char*)&build_info->odm_revision, NULL) <= 0 ||
	    property_get(BUILD_PROP_PLATFORM_VERSION, (char*)&build_info->platform_version, NULL) <= 0) {
		LOGE("Failed to get all properties\n");
		ret = -1;
	}
	return ret;
}

int odmcheck_compare_versions(struct odmcheck_version_info *info, struct odmcheck_version_info *build_info) {
	int ret = 0;
	ret = memcmp(info, build_info, sizeof(struct odmcheck_version_info));
	if (ret) {
		LOGE("Mismatch between versions, difference=%d\n", ret);
	} else {
		LOGV("ODM partition matches expectations!\n");
	}
	return ret;
}

static int odmcheck_draw_text(const char *str, int x, int y)
{
	int str_len_px = gr_measure(gr_sys_font(), str);

	if (x < 0)
		x = (gr_fb_width() - str_len_px) / 2;
	if (y < 0)
		y = (gr_fb_height() - char_height) / 2;
	gr_text(gr_sys_font(), x, y, str, 0);

	return y + char_height;
}

static void odmcheck_mk_version_str(struct odmcheck_version_info *info, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "Android: %s Kernel: %s Platform: %s ODM rev: %s",
	         info->android_version, info->kernel_version, info->platform_version, info->odm_revision);
}

/*
 * TODO: This horror needs to be fixed up.
 * The system font is only available in recovery builds, so some miniscule backup font is used.
 * There is no layouting, no calculations of font height, nor any screen limit checks.
 */
static int odmcheck_display_error(struct odmcheck_version_info *info, struct odmcheck_version_info *build_info)
{
	char buf[1024];
	gr_init();
	gr_font_size(gr_sys_font(), &char_width, &char_height);
	odmcheck_set_backlight(true);
	gr_color(0,128,255,255);
	gr_clear();
//	gr_color(128,255,0,255);
//	gr_fill(100, 100, 200, 200);
	gr_color(255,255,255,255);
	odmcheck_mk_version_str(info, buf, sizeof(buf));
	odmcheck_draw_text("odm_version.prop", 50, 300);
	odmcheck_draw_text(buf, 50, 350);
	odmcheck_mk_version_str(build_info, buf, sizeof(buf));
	odmcheck_draw_text("build.prop", 50, 400);
	odmcheck_draw_text(buf, 50, 450);
	gr_flip();
	sleep(10);
	odmcheck_set_backlight(false);
	gr_exit();
	return 0;
}

static void odmcheck_shutdown()
{
	LOGV("Shutting down everything...\n");
	property_set(SYS_PROP_POWERCTL, SYS_PROP_POWERCTL_SHUTDOWN);
}

int main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
	int ret = 0;
	struct odmcheck_version_info info, build_info;
	memset(&info, 0, sizeof(info));
	memset(&build_info, 0, sizeof(build_info));
	LOGV("ODM partition mounted = %d\n", odmcheck_dir_mounted(ODM_DIR));
	if (odmcheck_read_version_file(VERSION_FILE, &info)) {
		ret = -1;
	}
	if (!ret && odmcheck_read_build_prop(&build_info)) {
		ret = -1;
	}
	if (!ret && (!info.android_version[0] || !info.kernel_version[0] || !info.odm_revision[0] || !info.platform_version[0])) {
		LOGE("Missing properties\n");
		ret = -2;
	}
	if (!ret) {
		ret = odmcheck_compare_versions(&info, &build_info);
	}
	if (ret) {
		odmcheck_display_error(&info, &build_info);
#ifndef ODMCHECK_WARN_ONLY
		odmcheck_shutdown();
#endif
	}
	return ret;
}
