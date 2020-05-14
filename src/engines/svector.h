// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2017-2020, Intel Corporation */

#pragma once

#include "../pmemobj_engine.h"
#include "../polymorphic_string.h"

#include <libpmemobj++/container/vector.hpp>
#include <libpmemobj++/persistent_ptr.hpp>

namespace pmem
{
namespace kv
{
namespace internal
{
namespace svector
{
struct kv_container {
	using string_t = pmem::kv::polymorphic_string;
	using vector_t = pmem::obj::vector<string_t>;
	/**
	 * Keys and values are stored in separate vectors to optimize snapshotting.
	 * Keys are stored in order. (using binary search to guarantee)
	 */
	vector_t keys;
	vector_t values;
};
} /* namespace svector */
} /* namespace internal */

class svector : public pmemobj_engine_base<internal::svector::kv_container> {
public:
	svector(std::unique_ptr<internal::config> cfg);
	~svector();

	svector(const svector &) = delete;
	svector &operator=(const svector &) = delete;

	std::string name() final;

	status count_all(std::size_t &cnt) final;

	status get_all(get_kv_callback *callback, void *arg) final;
	status get_above(string_view key, get_kv_callback *callback, void *arg) final;
	status get_equal_above(string_view key, get_kv_callback *callback,
			       void *arg) final;
	status get_equal_below(string_view key, get_kv_callback *callback,
			       void *arg) final;
	status get_below(string_view key, get_kv_callback *callback, void *arg) final;

	status exists(string_view key) final;

	status get(string_view key, get_v_callback *callback, void *arg) final;

	status put(string_view key, string_view value) final;

	status remove(string_view key) final;

private:
	void Recover();
	int binary_search(string_view target);
	internal::svector::kv_container *container;
};

} /* namespace kv */
} /* namespace pmem */
