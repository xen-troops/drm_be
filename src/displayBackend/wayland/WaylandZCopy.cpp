/*
 *  WaylandZCopy class
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

#include "WaylandZCopy.hpp"

#include <algorithm>

#include "Exception.hpp"
#include "FrameBuffer.hpp"

using std::find;
using std::lock_guard;
using std::mutex;
using std::string;

using DisplayItf::DisplayBufferPtr;
using DisplayItf::FrameBufferPtr;

namespace Wayland {

/*******************************************************************************
 * WaylandZCopy
 ******************************************************************************/

WaylandZCopy::WaylandZCopy(wl_registry* registry,
						   uint32_t id, uint32_t version) :
	Registry(registry, id, version),
	mIsAuthenticated(false),
	mLog("WaylandZCopy")
{
}

/*******************************************************************************
 * Public
 ******************************************************************************/

DisplayBufferPtr
WaylandZCopy::createDumb(uint32_t width, uint32_t height, uint32_t bpp,
						 size_t offset, domid_t domId, GrantRefs& refs,
						 bool allocRefs)
{
	lock_guard<mutex> lock(mMutex);

	if (mDrmDevice)
	{
		return mDrmDevice->createDisplayBuffer(width, height, bpp, offset,
											 domId, refs, allocRefs);
	}

	throw Exception("Can't create dumb: no DRM device", ENOENT);
}

/*******************************************************************************
 * Protected
 ******************************************************************************/

void WaylandZCopy::onDevice(const string& name)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "onDevice name: " << name;

	try
	{
		mDrmDevice.reset(new Drm::DisplayWayland(name));
	}
	catch(const std::exception& err)
	{
		LOG(mLog, ERROR) << err.what();
	}

	authenticate();
}

void WaylandZCopy::onFormat(uint32_t format)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "onFormat format: 0x" << std::hex << format;

	mSupportedFormats.push_back(format);
}

void WaylandZCopy::onAuthenticated()
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "onAuthenticated";

	mIsAuthenticated = true;
}

bool WaylandZCopy::isPixelFormatSupported(uint32_t format)
{
	return find(mSupportedFormats.begin(), mSupportedFormats.end(), format)
		   != mSupportedFormats.end();
}

/*******************************************************************************
 * WaylandDrm
 ******************************************************************************/

WaylandDrm::WaylandDrm(wl_registry* registry,
					   uint32_t id, uint32_t version) :
	WaylandZCopy(registry, id, version),
	mWlDrm(nullptr)
{
	try
	{
		init();
	}
	catch(const std::exception& e)
	{
		release();

		throw;
	}
}

WaylandDrm::~WaylandDrm()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

FrameBufferPtr
WaylandDrm::createDrmBuffer(DisplayBufferPtr displayBuffer,
							uint32_t width,uint32_t height,
							uint32_t pixelFormat)
{
	lock_guard<mutex> lock(mMutex);

	if (!isPixelFormatSupported(pixelFormat))
	{
		throw Exception("Unsupported pixel format", EINVAL);
	}

	return  FrameBufferPtr(new DrmBuffer(mWlDrm, displayBuffer, width,
												height, pixelFormat));
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void WaylandDrm::sOnDevice(void *data, wl_drm *drm, const char *name)
{
	static_cast<WaylandDrm*>(data)->onDevice(name);
}

void WaylandDrm::sOnFormat(void *data, wl_drm *drm, uint32_t format)
{
	static_cast<WaylandDrm*>(data)->onFormat(format);
}

void WaylandDrm::sOnAuthenticated(void *data, wl_drm *drm)
{
	static_cast<WaylandDrm*>(data)->onAuthenticated();
}

void WaylandDrm::sOnCapabilities(void *data, wl_drm *drm, uint32_t value)
{
	static_cast<WaylandDrm*>(data)->onCapabilities(value);
}

void WaylandDrm::onCapabilities(uint32_t value)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "onCapabilities value: " << value;
}

void WaylandDrm::authenticate()
{
	wl_drm_authenticate(mWlDrm, mDrmDevice->getMagic());
}

void WaylandDrm::init()
{
	mWlDrm = static_cast<wl_drm*>(bind(&wl_drm_interface));

	if (!mWlDrm)
	{
		throw Exception("Can't bind drm", errno);
	}

	mWlListener = {sOnDevice, sOnFormat, sOnAuthenticated, sOnCapabilities};

	if (wl_drm_add_listener(mWlDrm, &mWlListener, this) < 0)
	{
		throw Exception("Can't add listener", errno);
	}

	LOG(mLog, DEBUG) << "Create wl drm";
}

void WaylandDrm::release()
{
	if (mWlDrm)
	{
		wl_drm_destroy(mWlDrm);

		LOG(mLog, DEBUG) << "Delete";
	}
}

/*******************************************************************************
 * Kms
 ******************************************************************************/

WaylandKms::WaylandKms(wl_registry* registry,
					   uint32_t id, uint32_t version) :
	WaylandZCopy(registry, id, version),
	mWlKms(nullptr)
{
	try
	{
		init();
	}
	catch(const std::exception& e)
	{
		release();

		throw;
	}
}

WaylandKms::~WaylandKms()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

FrameBufferPtr
WaylandKms::createKmsBuffer(DisplayBufferPtr displayBuffer,
							uint32_t width,uint32_t height,
							uint32_t pixelFormat)
{
	lock_guard<mutex> lock(mMutex);

	if (!isPixelFormatSupported(pixelFormat))
	{
		throw Exception("Unsupported pixel format", EINVAL);
	}

	return  FrameBufferPtr(new KmsBuffer(mWlKms, displayBuffer, width,
										 height, pixelFormat));
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void WaylandKms::sOnDevice(void *data, wl_kms *kms, const char *name)
{
	static_cast<WaylandKms*>(data)->onDevice(name);
}

void WaylandKms::sOnFormat(void *data, wl_kms *kms, uint32_t format)
{
	static_cast<WaylandKms*>(data)->onFormat(format);
}

void WaylandKms::sOnAuthenticated(void *data, wl_kms *kms)
{
	static_cast<WaylandKms*>(data)->onAuthenticated();
}

void WaylandKms::authenticate()
{
	wl_kms_authenticate(mWlKms, mDrmDevice->getMagic());
}

void WaylandKms::init()
{
	mWlKms = static_cast<wl_kms*>(bind(&wl_kms_interface));

	if (!mWlKms)
	{
		throw Exception("Can't bind kms", errno);
	}

	mWlListener = {sOnDevice, sOnFormat, sOnAuthenticated};

	if (wl_kms_add_listener(mWlKms, &mWlListener, this) < 0)
	{
		throw Exception("Can't add listener", errno);
	}

	LOG(mLog, DEBUG) << "Create";
}

void WaylandKms::release()
{
	if (mWlKms)
	{
		wl_kms_destroy(mWlKms);

		LOG(mLog, DEBUG) << "Delete";
	}
}

/*******************************************************************************
 * Linux dmabuf
 ******************************************************************************/

WaylandLinuxDmabuf::WaylandLinuxDmabuf(wl_registry* registry,
									   uint32_t id, uint32_t version) :
	WaylandZCopy(registry, id, version),
	mWlLinuxDmabuf(nullptr)
{
	try
	{
		init(version);
	}
	catch(const std::exception& e)
	{
		release();

		throw;
	}
}

WaylandLinuxDmabuf::~WaylandLinuxDmabuf()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

FrameBufferPtr
WaylandLinuxDmabuf::createLinuxDmabufBuffer(DisplayBufferPtr displayBuffer,
											uint32_t width,uint32_t height,
											uint32_t pixelFormat)
{
	lock_guard<mutex> lock(mMutex);

	if (!isPixelFormatSupported(pixelFormat))
	{
		throw Exception("Unsupported pixel format", EINVAL);
	}

	return  FrameBufferPtr(new LinuxDmabufBuffer(mWlLinuxDmabuf, displayBuffer,
												 width, height, pixelFormat));
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void WaylandLinuxDmabuf::sOnModifiers(void *data,
									  zwp_linux_dmabuf_v1 *zwpLinuxDmabuf,
									  uint32_t format, uint32_t modifierHi,
									  uint32_t modifierLo)
{
	/*
	 * Modifiers are described at
	 * https://www.khronos.org/registry/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import_modifiers.txt
	 *
	 * We do not support passing modifiers from the frontend to the backend,
	 * so ignore all formats with modifiers set.
	 *
	 * If IGNORE_MODIFIER_VALUES is defined, it will disable pixel format modifiers
	 * check. This option may be useful for a host system, which supports pixel formats
	 * without modifiers only, or a guest system with some specific pixel format
	 * with modifiers. In that case(i.e. IGNORE_MODIFIER_VALUES defined),
	 * some default settings may apply, and host-guest graphics buffers interaction
	 * may proceed without issues. NOTE: default behavior may also result in multiplane
	 * format choice, which is currently not supported by the protocol, which may lead
	 * to incorrect buffer interpretation by the host system.
	 */

#ifndef IGNORE_MODIFIER_VALUES
	if (modifierHi != 0 || modifierLo != 0)
	{
		return;
	}
#endif
	static_cast<WaylandLinuxDmabuf*>(data)->onFormat(format);
}

void WaylandLinuxDmabuf::sOnFormat(void *data,
								   zwp_linux_dmabuf_v1 *zwpLinuxDmabuf,
								   uint32_t format)
{
	/* This one is deprecated. */
}

void WaylandLinuxDmabuf::init(uint32_t version)
{
	if (version < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION)
	{
		throw Exception("Unsupported protocol version", errno);
	}

	mWlLinuxDmabuf = static_cast<zwp_linux_dmabuf_v1*>(
		bind(&zwp_linux_dmabuf_v1_interface));

	if (!mWlLinuxDmabuf)
	{
		throw Exception("Can't bind Linux dmabuf", errno);
	}

	mWlListener = {sOnFormat, sOnModifiers};

	if (zwp_linux_dmabuf_v1_add_listener(mWlLinuxDmabuf, &mWlListener, this) < 0)
	{
		throw Exception("Can't add listener", errno);
	}

	/*
	 * FIXME: Linux dmabuf interface doesn't provide any means to get the
	 * required DRM KMS device file name, so we ask for auto-detection here.
	 */
	onDevice("");

	LOG(mLog, DEBUG) << "Create";
}

void WaylandLinuxDmabuf::release()
{
	if (mWlLinuxDmabuf)
	{
		zwp_linux_dmabuf_v1_destroy(mWlLinuxDmabuf);

		LOG(mLog, DEBUG) << "Delete";
	}
}

}
