/*
 *  Buffers storage
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
 */

#ifndef SRC_BUFFERSSTORAGE_HPP_
#define SRC_BUFFERSSTORAGE_HPP_

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <xen/be/XenGnttab.hpp>

#include <xen/io/displif.h>

#include "DisplayItf.hpp"

using std::memcpy;
using std::min;
using std::move;

/***************************************************************************//**
 * Storage for display and frame buffers
 * @ingroup displ_be
 ******************************************************************************/
class BuffersStorage
{
public:

	/**
	 * @param domId   domain id
	 * @param display display object
	 */
	BuffersStorage(int domId, std::shared_ptr<DisplayItf> display);

	/**
	 * Creates display buffer
	 * @param dbCookie       display buffer cookie
	 * @param startDirectory grant table reference to the buffer start directory
	 * @param size           buffer size
	 * @param width          width in pixels
	 * @param height         height in pixels
	 * @param bpp            bits per pixel
	 */
	void createDisplayBuffer(uint64_t dbCookie,
							 grant_ref_t startDirectory, uint32_t size,
							 uint32_t width, uint32_t height, uint32_t bpp);

	/**
	 * Creates frame buffer
	 * @param dbCookie     display buffer cookie
	 * @param fbCookie     frame buffer cookie
	 * @param width        width in pixel
	 * @param height       height in pixel
	 * @param pixelFormat  pixel format
	 */
	void createFrameBuffer(uint64_t dbCookie, uint64_t fbCookie,
						   uint32_t width, uint32_t height,
						   uint32_t pixelFormat);

	/**
	 * Returns display buffer object
	 * @param dbCookie display buffer cookie
	 */
	std::shared_ptr<DisplayBufferItf> getDisplayBuffer(uint64_t dbCookie);

	/**
	 * Returns frame buffer object
	 * @param fbCookie frame buffer cookie
	 */
	std::shared_ptr<FrameBufferItf> getFrameBuffer(uint64_t fbCookie);

	/**
	 * Destroys display buffer
	 * @param dbCookie display buffer cookie
	 */
	void destroyDisplayBuffer(uint64_t dbCookie);

	/**
	 * Destroys frame buffer
	 * @param fbCookie frame buffer cookie
	 */
	void destroyFrameBuffer(uint64_t fbCookie);

	/**
	 * Copies data from  shared buffer to local display buffer
	 * @param fbCookie frame buffer cookie
	 */
	void copyBuffer(uint64_t fbCookie);

private:

	int mDomId;
	std::shared_ptr<DisplayItf> mDisplay;
	XenBackend::Log mLog;

	std::mutex mMutex;

	struct LocalDisplayBuffer
	{
		std::shared_ptr<DisplayBufferItf> displayBuffer;
		std::unique_ptr<XenBackend::XenGnttabBuffer> buffer;
	};

	std::unordered_map<uint64_t, std::shared_ptr<FrameBufferItf>> mFrameBuffers;
	std::unordered_map<uint64_t, LocalDisplayBuffer> mDisplayBuffers;

	std::shared_ptr<DisplayBufferItf>
		getDisplayBufferUnlocked(uint64_t dbCookie);
	std::shared_ptr<FrameBufferItf>
		getFrameBufferUnlocked(uint64_t fbCookie);

	void getBufferRefs(grant_ref_t startDirectory, uint32_t size,
					   std::vector<grant_ref_t>& refs);
};

#endif /* SRC_BUFFERSSTORAGE_HPP_ */