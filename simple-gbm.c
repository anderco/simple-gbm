/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <xf86drm.h>
#include <gbm.h>

#include <wayland-client.h>

#include "wayland-drm-client-protocol.h"

struct simple_gbm {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
	struct wl_drm *drm;
	int fd;
	struct gbm_device *gbm;
	struct gbm_bo *bo;
	int width, height;
	struct wl_buffer *buffer;
};

static void
allocate_bo(struct simple_gbm *simple_gbm)
{
	struct gbm_bo *bo;
	struct wl_buffer *buffer;
	int fd, ret;

	assert(simple_gbm->gbm);

	bo = gbm_bo_create(simple_gbm->gbm,
			   simple_gbm->width, simple_gbm->height,
			   GBM_FORMAT_XRGB8888, GBM_BO_USE_MAP);
	assert(bo);

	ret = gbm_bo_export_dma_buf(bo, &fd);
	assert(ret == 0);

	buffer = wl_drm_create_prime_buffer(simple_gbm->drm, fd,
					    simple_gbm->width,
					    simple_gbm->height,
					    GBM_FORMAT_XRGB8888,
					    0, gbm_bo_get_stride(bo),
					    0, 0, 0, 0);
	simple_gbm->bo = bo;
	simple_gbm->buffer = buffer;
}

static void
render(struct simple_gbm *simple_gbm)
{
	uint32_t *pixels;
	int stride;
	int x, y;

	if (!simple_gbm->bo)
		allocate_bo(simple_gbm);

	pixels = gbm_bo_map(simple_gbm->bo);
	stride = gbm_bo_get_stride(simple_gbm->bo);

	for (y = 0; y < simple_gbm->width; y++) {
		for (x = 0; x < simple_gbm->height; x++)
			pixels[x] = 0x00770077;
		pixels += stride / 4;
	}

	gbm_bo_unmap(simple_gbm->bo);

	wl_surface_attach(simple_gbm->surface, simple_gbm->buffer, 0, 0);
	wl_surface_damage(simple_gbm->surface, 0, 0,
			  simple_gbm->width, simple_gbm->height);
	wl_surface_commit(simple_gbm->surface);
}

static void
shell_surface_ping(void *data, struct wl_shell_surface *shell_surface,
		   uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

static void
shell_surface_configure(void *data, struct wl_shell_surface *shell_surface,
			uint32_t edges, int32_t width, int32_t height)
{
}

static void
shell_surface_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static struct wl_shell_surface_listener shell_surface_listener = {
	shell_surface_ping,
	shell_surface_configure,
	shell_surface_popup_done
};

static void
setup_window(struct simple_gbm *simple_gbm)
{
	assert(simple_gbm->compositor);
	assert(simple_gbm->shell);

	simple_gbm->surface =
		wl_compositor_create_surface(simple_gbm->compositor);

	simple_gbm->shell_surface =
		wl_shell_get_shell_surface(simple_gbm->shell,
					   simple_gbm->surface);

	wl_shell_surface_add_listener(simple_gbm->shell_surface,
				      &shell_surface_listener, simple_gbm);

	wl_shell_surface_set_toplevel(simple_gbm->shell_surface);
}

static void
drm_device(void *data, struct wl_drm *drm, const char *name)
{
	struct simple_gbm *simple_gbm = data;
	drm_magic_t magic;
	int fd, ret;

	fd = open(name, O_RDWR);
	assert(fd >= 0);

	ret = drmGetMagic(fd, &magic);
	assert(ret == 0);

	simple_gbm->fd = fd;

	wl_drm_authenticate(drm, magic);
	wl_display_roundtrip(simple_gbm->display);
}

static void
drm_format(void *data, struct wl_drm *drm, uint32_t format)
{
}

static void
drm_authenticated(void *data, struct wl_drm *drm)
{
	struct simple_gbm *simple_gbm = data;
	struct gbm_device *gbm;

	gbm = gbm_create_device(simple_gbm->fd);
	assert(gbm);

	simple_gbm->gbm = gbm;
}

static void
drm_capabilities(void *data, struct wl_drm *drm, uint32_t value)
{
}

static struct wl_drm_listener drm_listener = {
	drm_device,
	drm_format,
	drm_authenticated,
	drm_capabilities
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	struct simple_gbm *simple_gbm = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		simple_gbm->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface, 3);
	} else if (strcmp(interface, "wl_shell") == 0) {
		simple_gbm->shell =
			wl_registry_bind(registry, name, &wl_shell_interface, 1);
	} else if (strcmp(interface, "wl_drm") == 0) {
		simple_gbm->drm =
			wl_registry_bind(registry, name, &wl_drm_interface, 2);
		assert(simple_gbm->drm);
		wl_drm_add_listener(simple_gbm->drm, &drm_listener, simple_gbm);
		wl_display_roundtrip(simple_gbm->display);
	}
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove
};

static int
connect_to_display(struct simple_gbm *simple_gbm)
{
	struct wl_display *dpy;

	dpy = wl_display_connect(NULL);
	if (!dpy)
		return -1;

	simple_gbm->display = dpy;
	simple_gbm->registry = wl_display_get_registry(dpy);
	if (!simple_gbm->registry)
		return -1;

	wl_registry_add_listener(simple_gbm->registry, &registry_listener,
				 simple_gbm);
	wl_display_roundtrip(dpy);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct simple_gbm simple_gbm = { 0 };
	int ret = 0;

	if (connect_to_display(&simple_gbm) < 0) {
		fprintf(stderr, "Failed to connect to wayland display\n");
		return 1;
	}

	simple_gbm.width = 250;
	simple_gbm.height = 250;

	setup_window(&simple_gbm);

	render(&simple_gbm);

	/* run the main loop */
	while (ret != -1)
		ret = wl_display_dispatch(simple_gbm.display);

	/* cleanup */

	return 0;
}
