/*
 * Shell.cpp
 *
 *  Created on: Nov 24, 2016
 *      Author: al1
 */

#include "Shell.hpp"

#include "Exception.hpp"

namespace Wayland {

/*******************************************************************************
 * Shell
 ******************************************************************************/

Shell::Shell(wl_registry* registry, uint32_t id, uint32_t version) :
	Registry(registry, id, version),
	mWlShell(nullptr),
	mLog("Shell")
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

Shell::~Shell()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/
ShellSurfacePtr Shell::createShellSurface(SurfacePtr surface)
{
	LOG(mLog, DEBUG) << "Create shell surface";

	return ShellSurfacePtr(new ShellSurface(mWlShell, surface));
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void Shell::init()
{
	mWlShell = bind<wl_shell*>(&wl_shell_interface);

	if (!mWlShell)
	{
		throw Exception("Can't bind shell", errno);
	}

	LOG(mLog, DEBUG) << "Create";
}

void Shell::release()
{
	if (mWlShell)
	{
		wl_shell_destroy(mWlShell);

		LOG(mLog, DEBUG) << "Delete";
	}
}

}

