/*
 * Registry.hpp
 *
 *  Created on: Nov 24, 2016
 *      Author: al1
 */

#ifndef SRC_WAYLAND_REGISTRY_HPP_
#define SRC_WAYLAND_REGISTRY_HPP_

#include <cassert>
#include <wayland-client.h>

namespace Wayland {

/***************************************************************************//**
 * Base class for Wayland registry.
 * @ingroup wayland
 ******************************************************************************/
class Registry
{
protected:

	/**
	 * @param registry wl registry structure
	 * @param id       registry id
	 * @param version  registry version
	 */
	Registry(wl_registry* registry, uint32_t id, uint32_t version) :
		mWlRegistry(registry), mId(id), mVersion(version) {}

	virtual ~Registry() {}

	/**
	 * Returns registry id
	 */
	uint32_t getId() const { return mId; }

	/**
	 * Returns wl registry structure
	 */
	wl_registry* getRegistry() const { return mWlRegistry; }

	/**
	 * Returns registry version
	 */
	uint32_t getVersion() const { return mVersion; }

	/**
	 * Binds registry
	 */
	template<typename T>
	T bind(const wl_interface *interface)
	{
		checkPointer<T>();
		T ret = nullptr;
		if(interface){
			ret = static_cast<T>(wl_registry_bind(mWlRegistry, mId, interface, mVersion));
		}
		return ret;
	}

   	/**
	 * The template to identify if 'T' is pointer or not. If not a pointer - compilation error.
	 */
	template <typename T> struct allow_bind {static_assert(std::is_integral<T>::value, "Can only be pointer type");};
	template <typename T> struct allow_bind <T*> {};
	template <typename T> inline void checkPointer(){BindTest::allow_bind<T> ab;(void)ab;}

private:

	wl_registry* mWlRegistry;
	uint32_t mId;
	uint32_t mVersion;
};

}

#endif /* SRC_WAYLAND_REGISTRY_HPP_ */
