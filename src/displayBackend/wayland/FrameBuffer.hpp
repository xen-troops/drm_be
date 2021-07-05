/*
 *  Frame buffers class
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2016 EPAM Systems Inc.
 *
 */

#ifndef SRC_WAYLAND_FRAMEBUFFER_HPP_
#define SRC_WAYLAND_FRAMEBUFFER_HPP_

#include <mutex>

#include <wayland-client.h>

#include <xen/be/Log.hpp>

#include "Surface.hpp"

#include "DisplayItf.hpp"

#ifdef WITH_ZCOPY
#include "wayland-drm-client-protocol.h"
#include "wayland-kms-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#endif

namespace Wayland {

/***************************************************************************//**
 * Wl buffer class.
 * @ingroup wayland
 ******************************************************************************/
class WlBuffer : public DisplayItf::FrameBuffer
{
public:

	virtual ~WlBuffer();

	/**
	 * Gets WLBuffer
	 */
	wl_buffer* getWLBuffer()const{ return mWlBuffer;}

	/**
	 * Gets width
	 */
	uint32_t getWidth() const override { return mWidth; }

	/**
	 * Gets width
	 */
	uint32_t getHeight() const override { return mHeight; };

	/**
	 * Returns pointer to the display buffer
	 */
	DisplayItf::DisplayBufferPtr getDisplayBuffer() override
	{
		return mDisplayBuffer;
	}

	void setSurface(Surface* surface);

protected:

	DisplayItf::DisplayBufferPtr mDisplayBuffer;
	uint32_t mWidth;
	uint32_t mHeight;
	wl_buffer* mWlBuffer;
	XenBackend::Log mLog;

	WlBuffer(DisplayItf::DisplayBufferPtr displayBuffer,
			 uint32_t width, uint32_t height);

	void setListener();

private:

	wl_buffer_listener mWlListener;

	Surface* mSurface;

	std::mutex mMutex;

	static void sOnRelease(void *data, wl_buffer *wlBuffer);
	void onRelease();
};

/***************************************************************************//**
 * Shared buffer class.
 * @ingroup wayland
 ******************************************************************************/
class SharedBuffer : public WlBuffer
{
public:

	~SharedBuffer();

private:

	friend class SharedMemory;

	SharedBuffer(wl_shm* wlSharedMemory,
				 DisplayItf::DisplayBufferPtr displayBuffer,
				 uint32_t width, uint32_t height,
				 uint32_t pixelFormat);

	wl_shm_pool* mWlPool;

	void init(wl_shm* wlSharedMemory, uint32_t pixelFormat);
	void release();
};

typedef std::shared_ptr<SharedBuffer> SharedBufferPtr;

#ifdef WITH_ZCOPY
/***************************************************************************//**
 * KMS buffer class.
 * @ingroup wayland
 ******************************************************************************/
class KmsBuffer : public WlBuffer
{
private:

	friend class WaylandKms;

	KmsBuffer(wl_kms* wlKms, DisplayItf::DisplayBufferPtr displayBuffer,
			  uint32_t width, uint32_t height, uint32_t pixelFormat);
};

/***************************************************************************//**
 * DRM buffer class.
 * @ingroup wayland
 ******************************************************************************/
class DrmBuffer : public WlBuffer
{
private:

	friend class WaylandDrm;

	DrmBuffer(wl_drm* wlDrm, DisplayItf::DisplayBufferPtr displayBuffer,
			  uint32_t width, uint32_t height, uint32_t pixelFormat);
};

/***************************************************************************//**
 * Linux dmabuf buffer class.
 * @ingroup wayland
 ******************************************************************************/
class LinuxDmabufBuffer : public WlBuffer
{
private:

	friend class WaylandLinuxDmabuf;

	LinuxDmabufBuffer(zwp_linux_dmabuf_v1* wlLinuxDmabuf,
					  DisplayItf::DisplayBufferPtr displayBuffer,
					  uint32_t width, uint32_t height, uint32_t pixelFormat);
};

#endif

}

#endif /* SRC_WAYLAND_FRAMEBUFFER_HPP_ */
