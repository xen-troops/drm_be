/*
 *  Display class
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

#include "Display.hpp"

#include <signal.h>

#include "Exception.hpp"

using std::lock_guard;
using std::mutex;
using std::string;
using std::thread;

using DisplayItf::DisplayBufferPtr;
using DisplayItf::FrameBufferPtr;

#ifdef WITH_INPUT
using InputItf::KeyboardCallbacks;
using InputItf::PointerCallbacks;
using InputItf::TouchCallbacks;
#endif

using XenBackend::PollFd;

namespace Wayland {

/*******************************************************************************
 * Display
 ******************************************************************************/

Display::Display(bool disable_zcopy) :
	mWlDisplay(nullptr),
	mWlRegistry(nullptr),
	mDisableZCopy(disable_zcopy),
	mLog("Display")
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

Display::~Display()
{
	stop();

	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void Display::start()
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Start";

	mThread = thread(&Display::dispatchThread, this);
}

void Display::stop()
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Stop";

	if (mPollFd)
	{
		mPollFd->stop();
	}

	if (mThread.joinable())
	{
		mThread.join();
	}
}

void Display::flush()
{
	int result = 0;

	while((result = wl_display_flush(mWlDisplay) < 0) && (errno == EAGAIN));

	if (result < 0)
	{
		throw Exception("Can't flush events", errno);
	}
}

DisplayItf::ConnectorPtr Display::createConnector(domid_t domId,
												  const string& name,
												  uint32_t width,
												  uint32_t height)
{
	lock_guard<mutex> lock(mMutex);

	Connector* connector = nullptr;

#ifdef WITH_IVI_EXTENSION
	if (mIviApplication)
	{
		uint32_t surfaceId = 0;

		try
		{
			surfaceId = stoi(name);
		}
		catch(const std::exception& e)
		{
			throw Exception("Can't create surface id: " + name, EINVAL);
		}

		connector = new IviConnector(domId, name, mIviApplication, mCompositor,
									 surfaceId, width, height);

		LOG(mLog, DEBUG) << "Create ivi connector, name: " << name;
	}
	else
#endif
	if (mShell)
	{
		connector = new ShellConnector(domId, name, mShell, mCompositor,
									   width, height);

		LOG(mLog, DEBUG) << "Create shell connector, name: " << name;
	}
	else
	{
		connector = new Connector(domId, name, mCompositor, width, height);

		LOG(mLog, DEBUG) << "Create connector, name: " << name;
	}

	Wayland::ConnectorPtr connectorPtr(connector);

	return connectorPtr;
}

DisplayBufferPtr Display::createDisplayBuffer(
		uint32_t width, uint32_t height, uint32_t bpp, size_t offset)
{
	lock_guard<mutex> lock(mMutex);

	if (mSharedMemory)
	{
		return mSharedMemory->createSharedFile(width, height, bpp, offset);
	}

	throw Exception("Can't create display buffer", ENOENT);
}

DisplayBufferPtr Display::createDisplayBuffer(
		uint32_t width, uint32_t height, uint32_t bpp, size_t offset,
		domid_t domId, GrantRefs& refs, bool allocRefs)
{
	lock_guard<mutex> lock(mMutex);

#ifdef WITH_ZCOPY

	if (mWaylandDrm)
	{
		return mWaylandDrm->createDumb(width, height, bpp, offset,
									   domId, refs, allocRefs);
	}

	if (mWaylandKms)
	{
		return mWaylandKms->createDumb(width, height, bpp, offset,
									   domId, refs, allocRefs);
	}

	if (mWaylandLinuxDmabuf)
	{
		return mWaylandLinuxDmabuf->createDumb(width, height, bpp, offset,
											   domId, refs, allocRefs);
	}

#endif

	if (mSharedMemory)
	{
		return mSharedMemory->createSharedFile(width, height, bpp, offset,
											   domId, refs);
	}

	throw Exception("Can't create display buffer", ENOENT);
}

FrameBufferPtr Display::createFrameBuffer(DisplayBufferPtr displayBuffer,
										  uint32_t width, uint32_t height,
										  uint32_t pixelFormat)
{
	lock_guard<mutex> lock(mMutex);

#ifdef WITH_ZCOPY

	if (mWaylandDrm)
	{
		return mWaylandDrm->createDrmBuffer(displayBuffer, width, height,
											pixelFormat);
	}

	if (mWaylandKms)
	{
		return mWaylandKms->createKmsBuffer(displayBuffer, width, height,
											pixelFormat);
	}

	if (mWaylandLinuxDmabuf)
	{
		return mWaylandLinuxDmabuf->createLinuxDmabufBuffer(displayBuffer,
															width, height,
															pixelFormat);
	}

#endif

	if (mSharedMemory)
	{
		return mSharedMemory->createSharedBuffer(displayBuffer, width, height,
												 pixelFormat);
	}

	throw Exception("Can't create frame buffer", ENOENT);
}


#ifdef WITH_INPUT

template<>
void Display::setInputCallbacks<KeyboardCallbacks>(
		const string& connector, const KeyboardCallbacks& callbacks)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Set keyboard callback for connector: " << connector;

	if (mSeat)
	{
		auto keyboard = mSeat->getKeyboard();

		if (keyboard)
		{
			keyboard->setConnectorCallbacks(connector, callbacks);
		}
		else
		{
			LOG(mLog, WARNING) << "No keyboard for input callbacks";
		}
	}
	else
	{
		LOG(mLog, WARNING) << "No seat for input callbacks";
	}
}

template<>
void Display::clearInputCallbacks<KeyboardCallbacks>(const string& connector)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Clear keyboard callback for connector: " << connector;

	if (mSeat)
	{
		auto keyboard = mSeat->getKeyboard();

		if (keyboard)
		{
			keyboard->clearConnectorCallbacks(connector);
		}
	}
}

template<>
void Display::setInputCallbacks<PointerCallbacks>(
		const string& connector, const PointerCallbacks& callbacks)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Set pointer callback for connector: " << connector;

	if (mSeat)
	{
		auto pointer = mSeat->getPointer();

		if (pointer)
		{
			pointer->setConnectorCallbacks(connector, callbacks);
		}
		else
		{
			LOG(mLog, WARNING) << "No pointer for input callbacks";
		}
	}
	else
	{
		LOG(mLog, WARNING) << "No seat for input callbacks";
	}
}

template<>
void Display::clearInputCallbacks<PointerCallbacks>(const string& connector)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Clear pointer callback for connector: " << connector;

	if (mSeat)
	{
		auto pointer = mSeat->getPointer();

		if (pointer)
		{
			pointer->clearConnectorCallbacks(connector);
		}
	}
}

template<>
void Display::setInputCallbacks<TouchCallbacks>(
		const string& connector, const TouchCallbacks& callbacks)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Set touch callback for connector: " << connector;

	if (mSeat)
	{
		auto touch = mSeat->getTouch();

		if (touch)
		{
			touch->setConnectorCallbacks(connector, callbacks);
		}
		else
		{
			LOG(mLog, WARNING) << "No touch for input callbacks";
		}
	}
	else
	{
		LOG(mLog, WARNING) << "No seat for input callbacks";
	}
}

template<>
void Display::clearInputCallbacks<TouchCallbacks>(const string& connector)
{
	lock_guard<mutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Clear touch callback for connector: " << connector;

	if (mSeat)
	{
		auto touch = mSeat->getTouch();

		if (touch)
		{
			touch->clearConnectorCallbacks(connector);
		}
	}
}

#endif
/*******************************************************************************
 * Private
 ******************************************************************************/

void Display::sWaylandLog(const char* fmt, va_list arg)
{
	auto len = vsnprintf(nullptr, 0, fmt, arg);

	char message[len + 1];

	vsprintf(message, fmt, arg);

	LOG("Wayland", ERROR) << message;
}

void Display::sRegistryHandler(void *data, wl_registry *registry, uint32_t id,
							   const char *interface, uint32_t version)
{
	try
	{
		static_cast<Display*>(data)->registryHandler(registry, id, interface, version);
	}
	catch(const std::exception& err)
	{
		LOG("Wayland", ERROR) << err.what();
	}
}

void Display::sRegistryRemover(void *data, struct wl_registry *registry,
							   uint32_t id)
{
	static_cast<Display*>(data)->registryRemover(registry, id);
}

void Display::registryHandler(wl_registry *registry, uint32_t id,
							  const std::string& interface, uint32_t version)
{
	LOG(mLog, DEBUG) << "Registry event, itf: " << interface << ", id: " << id
					 << ", version: " << version;

	if (interface == "wl_compositor")
	{
		mCompositor.reset(new Compositor(mWlDisplay, registry, id, version));
	}
	else if (interface == "wl_shell")
	{
		mShell.reset(new Shell(registry, id, version));
	}
	else if (interface == "wl_shm")
	{
		mSharedMemory.reset(new SharedMemory(registry, id, version));
	}
#ifdef WITH_IVI_EXTENSION
	else if (interface == "ivi_application")
	{
		mIviApplication.reset(new IviApplication(registry, id, version));
	}
#endif
#ifdef WITH_INPUT
	else if (interface == "wl_seat")
	{
		mSeat.reset(new Seat(registry, id, Seat::cVersion));
	}
#endif
#ifdef WITH_ZCOPY
	if (!mDisableZCopy)
	{
		if (interface == "wl_drm")
		{
			mWaylandDrm.reset(new WaylandDrm(registry, id, version));
		}
		else if (interface == "wl_kms")
		{
			mWaylandKms.reset(new WaylandKms(registry, id, version));
		}
		else if (interface == "zwp_linux_dmabuf_v1")
		{
			mWaylandLinuxDmabuf.reset(new WaylandLinuxDmabuf(registry, id, version));
		}
	}
#endif
}

void Display::registryRemover(wl_registry *registry, uint32_t id)
{
	LOG(mLog, DEBUG) << "Registry removed event, id: " << id;
}

void Display::init()
{
	wl_log_set_handler_client(sWaylandLog);

	mWlDisplay = wl_display_connect(nullptr);

	if (!mWlDisplay)
	{
		throw Exception("Can't connect to display", errno);
	}

	mPollFd.reset(new PollFd(wl_display_get_fd(mWlDisplay), POLLIN));

	LOG(mLog, DEBUG) << "Connected";

	mWlRegistryListener = {sRegistryHandler, sRegistryRemover};

	mWlRegistry = wl_display_get_registry(mWlDisplay);

	if (!mWlRegistry)
	{
		throw Exception("Can't get registry", errno);
	}

	if(wl_registry_add_listener(mWlRegistry, &mWlRegistryListener, this) == -1)
	{
		throw Exception("Can not add the listener.", errno);
	};

	wl_display_dispatch(mWlDisplay);
	wl_display_roundtrip(mWlDisplay);

	if (!mCompositor)
	{
		throw Exception("Can't get compositor", ENOENT);
	}
}

void Display::release()
{
	// clear connectors first as it keeps Surfaces which should be deleted
	// prior IviApplication

#ifdef WITH_IVI_EXTENSION
	mIviApplication.reset();
#endif
	mShell.reset();
	mSharedMemory.reset();
	mCompositor.reset();
#ifdef WITH_INPUT
	mSeat.reset();
#endif
#ifdef WITH_ZCOPY
	mWaylandDrm.reset();
	mWaylandKms.reset();
	mWaylandLinuxDmabuf.reset();
#endif

	if (mWlRegistry)
	{
		wl_registry_destroy(mWlRegistry);
	}

	if (mWlDisplay)
	{
		flush();
		wl_display_disconnect(mWlDisplay);

		LOG(mLog, DEBUG) << "Disconnected";
	}
}

int Display::checkWaylandError()
{
	auto err = wl_display_get_error(mWlDisplay);

	if (err)
	{
		if (err == EPROTO)
		{
			const wl_interface *interface;
			uint32_t id;

			auto code = wl_display_get_protocol_error(mWlDisplay, &interface,
													  &id);
			LOG(mLog, ERROR) << "Wayland proto error, itf: "
							 << interface->name
							 << ", code: " << code;
		}
		else
		{
			LOG(mLog, ERROR) << "Wayland error, code: " << strerror(err);
		}
	}

	return err;
}

void Display::dispatchThread()
{
	try
	{
		bool terminate = false;

		while(!terminate)
		{
			while (wl_display_prepare_read(mWlDisplay) != 0)
			{
				auto val = wl_display_dispatch_pending(mWlDisplay);

				if (val < 0)
				{
					throw Exception("Can't dispatch pending events", errno);
				}

				DLOG(mLog, DEBUG) << "Dispatch events: " << val;
			}

			flush();

			if (mPollFd->poll())
			{
				wl_display_read_events(mWlDisplay);
			}
			else
			{
				terminate = true;
			}
		}
	}
	catch(const std::exception& e)
	{
		if (!checkWaylandError())
		{
			LOG(mLog, ERROR) << e.what();
		}

		kill(getpid(), SIGTERM);
	}

	wl_display_cancel_read(mWlDisplay);
}

}
