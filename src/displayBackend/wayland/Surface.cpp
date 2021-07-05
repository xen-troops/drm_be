/*
 * Surface.cpp
 *
 *  Created on: Nov 24, 2016
 *      Author: al1
 */

#include "Surface.hpp"

#include "Exception.hpp"
#include "FrameBuffer.hpp"

using std::chrono::milliseconds;
using std::mutex;
using std::thread;
using std::unique_lock;

using DisplayItf::FrameBufferPtr;

namespace Wayland {

/*******************************************************************************
 * Surface
 ******************************************************************************/

Surface::Surface(wl_compositor* compositor) :
	mWlSurface(nullptr),
	mWlFrameCallback(nullptr),
	mBuffer(nullptr),
	mTerminate(false),
	mWaitForFrame(false),
	mLog("Surface")
{
	try
	{
		init(compositor);
	}
	catch(const std::exception& e)
	{
		release();

		throw;
	}
}

Surface::~Surface()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void Surface::draw(FrameBufferPtr frameBuffer,
				   FrameCallback callback)
{
	unique_lock<mutex> lock(mMutex);

	DLOG(mLog, DEBUG) << "Draw";

	mStoredCallback = callback;

	if (mStoredCallback && !mWaitForFrame)
	{
		if (!mWlFrameCallback)
		{
			mWlFrameCallback = wl_surface_frame(mWlSurface);

			if (!mWlFrameCallback)
			{
				throw Exception("Can't get frame callback", errno);
			}

			if (wl_callback_add_listener(mWlFrameCallback,
					&mWlFrameListener, this) < 0)
			{
				throw Exception("Can't add listener", errno);
			}
		}
	}

	mBuffer = dynamic_cast<WlBuffer*>(frameBuffer.get());

	if (mBuffer == nullptr)
	{
		throw Exception("FrameBuffer must have type WlBuffer.", EINVAL);
	}

	mBuffer->setSurface(this);
	
	wl_surface_damage(mWlSurface, 0, 0,
					  mBuffer->getWidth(),
					  mBuffer->getHeight());

	wl_surface_attach(mWlSurface, mBuffer->getWLBuffer(), 0, 0);

	wl_surface_commit(mWlSurface);

	mCondVar.notify_one();
}

void Surface::clear()
{
	unique_lock<mutex> lock(mMutex);

	DLOG(mLog, DEBUG) << "Clear";

	mBuffer = nullptr;

	wl_surface_attach(mWlSurface, nullptr, 0, 0);

	wl_surface_commit(mWlSurface);
}

/*******************************************************************************
 * Private
 ******************************************************************************/
void Surface::sFrameHandler(void *data, wl_callback *wl_callback,
							uint32_t callback_data)
{
	static_cast<Surface*>(data)->frameHandler();
}

void Surface::frameHandler()
{
	unique_lock<mutex> lock(mMutex);

	DLOG(mLog, DEBUG) << "Frame handler";

	wl_callback_destroy(mWlFrameCallback);

	mWlFrameCallback = nullptr;

	sendCallback();

	if (mWaitForFrame)
	{
		mWaitForFrame = false;

		LOG(mLog, DEBUG) << "Surface is active";
	}
	else
	{
		mCondVar.notify_one();
	}
}

void Surface::sendCallback()
{
	if (mStoredCallback)
	{
		mStoredCallback();

		mStoredCallback = nullptr;
	}
}

void Surface::run()
{
	unique_lock<mutex> lock(mMutex);

	while(!mTerminate)
	{
		if (!mStoredCallback)
		{
			mCondVar.wait(lock);
		}
		else
		{
			if (mWaitForFrame ||
				!mCondVar.wait_for(lock, milliseconds(cFrameTimeoutMs),
								   [this] { return !mStoredCallback; }))
			{
				if (!mWaitForFrame)
				{
					mWaitForFrame = true;

					LOG(mLog, DEBUG) << "Surface is inactive";
				}

				sendCallback();
			}
		}
	}
}

void Surface::stop()
{
	unique_lock<mutex> lock(mMutex);

	mTerminate = true;

	mCondVar.notify_one();
}

void Surface::init(wl_compositor* compositor)
{
	mWlSurface = wl_compositor_create_surface(compositor);

	if (!mWlSurface)
	{
		throw Exception("Can't create surface", errno);
	}

	mWlFrameListener = { sFrameHandler };

	mThread = thread(&Surface::run, this);

	LOG(mLog, DEBUG) << "Create: " << mWlSurface;
}

void Surface::release()
{
	if (mBuffer)
	{
		mBuffer->setSurface(nullptr);
	}

	clear();

	stop();

	if (mThread.joinable())
	{
		mThread.join();
	}

	if (mWlFrameCallback)
	{
		wl_callback_destroy(mWlFrameCallback);
	}

	if (mWlSurface)
	{
		wl_surface_destroy(mWlSurface);

		LOG(mLog, DEBUG) << "Delete: " << mWlSurface;
	}
}

}
