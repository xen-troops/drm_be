/*
 * Surface.hpp
 *
 *  Created on: Nov 24, 2016
 *      Author: al1
 */

#ifndef SRC_WAYLAND_SURFACE_HPP_
#define SRC_WAYLAND_SURFACE_HPP_

#include <condition_variable>
#include <mutex>
#include <thread>

#include <wayland-client.h>

#include <xen/be/Log.hpp>

#include "DisplayItf.hpp"

namespace Wayland {

class WlBuffer;

/***************************************************************************//**
 * Wayland surface class.
 * @ingroup wayland
 ******************************************************************************/
class Surface
{
public:

	/**
	 * Callback which is called when frame is displayed
	 */
	typedef std::function<void()> FrameCallback;

	~Surface();

	/**
	 * Draws shared buffer content
	 * @param frameBuffer  shared buffer
	 * @param callback     called when frame with the content is displayed
	 */
	void draw(DisplayItf::FrameBufferPtr frameBuffer,
			  FrameCallback callback = nullptr);

	/**
	 * Clear surface
	 */
	void clear();

private:

	friend class Display;
	friend class IviSurface;
	template <typename T> friend class SeatDevice;
	friend class ShellSurface;
	friend class Compositor;
	friend class Connector;

	const uint32_t cFrameTimeoutMs = 50;

	explicit Surface(wl_compositor* compositor);

	wl_surface* mWlSurface;
	wl_callback *mWlFrameCallback;
	WlBuffer* mBuffer;
	bool mTerminate;
	bool mWaitForFrame;
	XenBackend::Log mLog;

	std::mutex mMutex;
	std::condition_variable mCondVar;
	std::thread mThread;

	wl_callback_listener mWlFrameListener;

	FrameCallback mStoredCallback;

	static void sFrameHandler(void *data, wl_callback *wl_callback,
							  uint32_t callback_data);
	void frameHandler();

	void sendCallback();

	void run();
	void stop();

	void init(wl_compositor* compositor);
	void release();
};

typedef std::shared_ptr<Surface> SurfacePtr;

}

#endif /* SRC_WAYLAND_SURFACE_HPP_ */
