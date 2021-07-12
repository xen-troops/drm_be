/*
 * IviApplication.cpp
 *
 *  Created on: Dec 8, 2016
 *      Author: al1
 */

#include "IviApplication.hpp"

#include "Exception.hpp"

namespace Wayland {

/*******************************************************************************
 * IviApplication
 ******************************************************************************/

IviApplication::IviApplication(wl_registry* registry,
							   uint32_t id, uint32_t version) :
	Registry(registry, id, version),
	mWlIviApplication(nullptr),
	mLog("IviApplication")
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

IviApplication::~IviApplication()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

IviSurfacePtr IviApplication::createIviSurface(SurfacePtr surface,
											   uint32_t surfaceId)
{
	LOG(mLog, DEBUG) << "Create ivi surface, id: " << surfaceId;

	return IviSurfacePtr(new IviSurface(mWlIviApplication, surface, surfaceId));
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void IviApplication::init()
{
	mWlIviApplication = bind<ivi_application*>(&ivi_application_interface);

	if (!mWlIviApplication)
	{
		throw Exception("Can't bind IVI application", errno);
	}

	LOG(mLog, DEBUG) << "Create";

}

void IviApplication::release()
{
	if (mWlIviApplication)
	{
		ivi_application_destroy(mWlIviApplication);

		LOG(mLog, DEBUG) << "Delete";
	}
}

}
