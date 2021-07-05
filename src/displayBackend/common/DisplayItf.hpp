/*
 *  Display Interface
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

#ifndef SRC_DISPLAYITF_HPP_
#define SRC_DISPLAYITF_HPP_

#include <exception>
#include <functional>
#include <memory>
#include <vector>

extern "C" {
#include <xenctrl.h>
#include "displif.h"
}

#include "PgDirSharedBuffer.hpp"

namespace DisplayItf {

/***************************************************************************//**
 * @defgroup display_itf Display interface
 * Abstract classes for display implementation.
 ******************************************************************************/

/***************************************************************************//**
 * Provides display buffer functionality.
 * @ingroup display_itf
 ******************************************************************************/
class DisplayBuffer
{
public:

	virtual ~DisplayBuffer() {}

	/**
	 * Returns buffer size
	 */
	virtual size_t getSize() const = 0;

	/**
	 * Returns pointer to the data buffer
	 */
	virtual void* getBuffer() const = 0;

	/**
	 * Gets stride
	 */
	virtual uint32_t getStride() const = 0;

	/**
	 * Gets handle
	 */
	virtual uintptr_t getHandle() const = 0;

	/**
	 * Gets fd
	 */
	virtual int getFd() const = 0;

	/**
	 * Reads name
	 */
	virtual uint32_t readName() = 0;

	/**
	 * Indicates if copy operation shall be applied
	 */
	virtual bool needsCopy() = 0;

	/**
	 * Copies data from associated grant table buffer
	 */
	virtual void copy() = 0;

};

typedef std::shared_ptr<DisplayBuffer> DisplayBufferPtr;

/***************************************************************************//**
 * Provides frame buffer functionality.
 * @ingroup display_itf
 ******************************************************************************/
class FrameBuffer
{
public:

	virtual ~FrameBuffer() {};

	/**
	 * Gets width
	 */
	virtual uint32_t getWidth() const = 0;

	/**
	 * Gets width
	 */
	virtual uint32_t getHeight() const = 0;

	/**
	 * Returns pointer to the display buffer
	 */
	virtual DisplayBufferPtr getDisplayBuffer() = 0;
};


typedef std::shared_ptr<FrameBuffer> FrameBufferPtr;

/***************************************************************************//**
 * Provides connector functionality.
 * @ingroup display_itf
 ******************************************************************************/
class Connector
{
public:

	/**
	 * Callback which is called when page flip is done
	 */
	typedef std::function<void()> FlipCallback;

	virtual ~Connector() {};

	/**
	 * Returns connector name
	 */
	virtual std::string getName() const = 0;

	/**
	 * Checks if the connector is connected
	 * @return <i>true</i> if connected
	 */
	virtual bool isConnected() const = 0;

	/**
	 * Checks if the connector is initialized
	 * @return <i>true</i> if initialized
	 */
	virtual bool isInitialized() const = 0;

	/**
	 * Initializes connector
	 * @param width       width
	 * @param height      height
	 * @param frameBuffer frame buffer
	 */
	virtual void init(uint32_t width, uint32_t height,
					  FrameBufferPtr frameBuffer) = 0;

	/**
	 * Releases initialized connector
	 */
	virtual void release() = 0;

	/**
	 * Performs page flip
	 * @param frameBuffer frame buffer to flip
	 * @param cbk         callback
	 */
	virtual void pageFlip(FrameBufferPtr frameBuffer, FlipCallback cbk) = 0;

	/**
	 * Queries connector's EDID
	 * @param  startDirectory grant table reference to the buffer start directory
	 * @param  size           buffer size
	 * @return size of the EDID placed in the buffer
	 */
	virtual size_t getEDID(grant_ref_t startDirectory, uint32_t size) = 0;
};

typedef std::shared_ptr<Connector> ConnectorPtr;

/***************************************************************************//**
 * Display interface class.
 * @ingroup display_itf
 ******************************************************************************/
class Display
{
public:

	virtual ~Display() {}

	/**
	 * Starts events handling
	 */
	virtual void start() = 0;

	/**
	 * Stops events handling
	 */
	virtual void stop() = 0;

	/**
	 * Flushes events
	 */
	virtual void flush() = 0;

	/**
	 * Creates connector
	 * @param domId  domain id
	 * @param name   connector name
	 * @param width  connector width as configured in XenStore
	 * @param height connector height as configured in XenStore
	 */
	virtual ConnectorPtr createConnector(domid_t domId,
										 const std::string& name,
										 uint32_t width, uint32_t height) = 0;

	/**
	 * Creates display buffer
	 * @param width  width
	 * @param height height
	 * @param bpp    bits per pixel
	 * @param offset offset of the data in the buffer
	 * @return shared pointer to the display buffer
	 */
	virtual DisplayBufferPtr createDisplayBuffer(
			uint32_t width, uint32_t height, uint32_t bpp,
			size_t offset) = 0;

	/**
	 * Creates display buffer with associated grant table buffer
	 * @param width     width
	 * @param height    height
	 * @param bpp       bits per pixel
	 * @param offset    offset of the data in the buffer
	 * @param domId     domain ID
	 * @param refs      vector of grant table reference
	 * @param allocRefs indicates that grant refs should be allocated on
	 * backend side
	 * @return shared pointer to the display buffer
	 */
	virtual DisplayBufferPtr createDisplayBuffer(
			uint32_t width, uint32_t height, uint32_t bpp, size_t offset,
			uint16_t domId, GrantRefs& refs, bool allocRefs) = 0;

	/**
	 * Creates frame buffer
	 * @param displayBuffer pointer to the display buffer
	 * @param width         width
	 * @param height        height
	 * @param pixelFormat   pixel format
	 * @return shared pointer to the frame buffer
	 */
	virtual FrameBufferPtr createFrameBuffer(DisplayBufferPtr displayBuffer,
											 uint32_t width,uint32_t height,
											 uint32_t pixelFormat) = 0;
};

typedef std::shared_ptr<Display> DisplayPtr;

}

#endif /* SRC_DISPLAYITF_HPP_ */
