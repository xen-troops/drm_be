/*
 * SharedMemory.cpp
 *
 *  Created on: Nov 24, 2016
 *      Author: al1
 */

#include "SharedMemory.hpp"

#include <algorithm>

#include "Exception.hpp"

using std::find;
using std::hex;
using std::setfill;
using std::setw;

using DisplayItf::DisplayBufferPtr;

#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888           0x34325241
#endif
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888           0x34325258
#endif

namespace Wayland {

/*******************************************************************************
 * SharedMemory
 ******************************************************************************/

SharedMemory::SharedMemory(wl_registry* registry, uint32_t id, uint32_t version) :
	Registry(registry, id, version),
	mWlSharedMemory(nullptr),
	mLog("SharedMemory")
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

SharedMemory::~SharedMemory()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

SharedFilePtr SharedMemory::createSharedFile(
		uint32_t width, uint32_t height, uint32_t bpp, size_t offset,
		domid_t domId, const GrantRefs& refs)
{
	LOG(mLog, DEBUG) << "Create shared file";

	return SharedFilePtr(new SharedFile(width, height, bpp, offset,
										domId, refs));
}

SharedBufferPtr SharedMemory::createSharedBuffer(
		DisplayBufferPtr displayBuffer, uint32_t width, uint32_t height,
		uint32_t pixelFormat)
{
	LOG(mLog, DEBUG) << "Create shared buffer";

	auto format = convertPixelFormat(pixelFormat);

	if (!isPixelFormatSupported(format))
	{
		throw Exception("Unsupported pixel format", EINVAL);
	}

	return SharedBufferPtr(new SharedBuffer(mWlSharedMemory,
											displayBuffer,
											width, height,
											format));
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void SharedMemory::sFormatHandler(void *data, wl_shm *wlShm, uint32_t format)
{
	static_cast<SharedMemory*>(data)->formatHandler(format);
}

void SharedMemory::formatHandler(uint32_t format)
{
	LOG(mLog, DEBUG) << "Format: 0x" << hex << setfill('0') << setw(8)
					 << format;

	mSupportedFormats.push_back(format);
}

void SharedMemory::init()
{
	mWlSharedMemory = bind<wl_shm*>(&wl_shm_interface);

	if (!mWlSharedMemory)
	{
		throw Exception("Can't bind shared memory", errno);
	}

	mWlListener = {sFormatHandler};

	if (wl_shm_add_listener(mWlSharedMemory, &mWlListener, this) < 0)
	{
		throw Exception("Can't add listener", errno);
	}

	LOG(mLog, DEBUG) << "Create";
}

void SharedMemory::release()
{
	if (mWlSharedMemory)
	{
		wl_shm_destroy(mWlSharedMemory);

		LOG(mLog, DEBUG) << "Delete";
	}
}

uint32_t SharedMemory::convertPixelFormat(uint32_t format)
{
	// WL format matches DRM format except two following values
	// (see: wl_shm_format)

	if (format == DRM_FORMAT_ARGB8888)
	{
		return WL_SHM_FORMAT_ARGB8888;
	}

	if (format == DRM_FORMAT_XRGB8888)
	{
		return WL_SHM_FORMAT_XRGB8888;
	}

	return format;
}

bool SharedMemory::isPixelFormatSupported(uint32_t format)
{
	return find(mSupportedFormats.begin(), mSupportedFormats.end(), format)
		   != mSupportedFormats.end();
}

}
