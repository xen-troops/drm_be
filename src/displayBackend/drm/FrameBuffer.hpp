/*
 *  FrameBuffer class
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

#ifndef SRC_DRM_FRAMEBUFFER_HPP_
#define SRC_DRM_FRAMEBUFFER_HPP_

#include <functional>

#include <xen/be/Log.hpp>

#include "DisplayItf.hpp"

namespace Drm {

/***************************************************************************//**
 * Provides DRM frame buffer functionality.
 * @ingroup drm
 ******************************************************************************/
class FrameBuffer : public DisplayItf::FrameBuffer
{
public:

	/**
	 * @param dumb        dumb
	 * @param width       frame buffer width
	 * @param height      frame buffer height
	 * @param pixelFormat frame buffer pixel format
	 */
	FrameBuffer(int drmFd, DisplayItf::DisplayBufferPtr displayBuffer,
				uint32_t width, uint32_t height,
				uint32_t pixelFormat);

	~FrameBuffer();

	/*
	 * Gets frameID
	*/
     uint32_t getID()const {return mId;}

	/**
	 * Gets width
	 */
	uint32_t getWidth() const override { return mWidth; }

	/**
	 * Gets width
	 */
	uint32_t getHeight() const override { return mHeight; }

	/**
	 * Returns pointer to the display buffer
	 */
	DisplayItf::DisplayBufferPtr getDisplayBuffer() override
	{
		return mDisplayBuffer;
	}

private:
	int mDrmFd;
	DisplayItf::DisplayBufferPtr mDisplayBuffer;
	uint32_t mWidth;
	uint32_t mHeight;
	uint32_t mId;
	XenBackend::Log mLog;

	void init(uint32_t pixelFormat);
	void release();
};

}

#endif /* SRC_DRM_FRAMEBUFFER_HPP_ */
