// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2017-2020, Intel Corporation */

#include "svector.h"
#include "../out.h"

#include <unistd.h>
#include <algorithm>
#include <cassert>

namespace pmem
{
namespace kv
{

svector::svector(std::unique_ptr<internal::config> cfg) : pmemobj_engine_base(cfg)
{
	static_assert(
		sizeof(internal::svector::kv_container::string_t) == 40,
		"Wrong size of svector value and key. This probably means that std::string has size > 32");

	LOG("Started ok");
	Recover();
}

svector::~svector()
{
	LOG("Stopped ok");
}

std::string svector::name()
{
	return "svector";
}

status svector::count_all(std::size_t &cnt)
{
	LOG("count_all");
	check_outside_tx();
	// check keys and values are in sync
	assert(container->keys.size() == container->values.size());
	cnt = container->keys.size();

	return status::OK;
}

status svector::get_all(get_kv_callback *callback, void *arg)
{
	LOG("get_all");
	check_outside_tx();

	assert(container->keys.size() == container->values.size());
	for (unsigned i = 0; i < container->keys.size(); i++) {
		auto ret = callback(container->keys[i].c_str(), container->keys[i].size(),
				    container->values[i].c_str(), container->values[i].size(), arg);

		if (ret != 0)
			return status::STOPPED_BY_CB;
	}

	return status::OK;
}

// (key, end), above key
status svector::get_above(string_view key, get_kv_callback *callback, void *arg)
{
	LOG("get_above start key>=" << std::string(key.data(), key.size()));
	check_outside_tx();
	auto it = std::upper_bound(container->keys.begin(), container->keys.end(),
				   key);
	size_t index = static_cast<size_t>(std::distance(container->keys.begin(), it));

	while (it != container->keys.end()) {
		auto ret = callback((*it).c_str(), (*it).size(),
				    container->values[index].c_str(),
				    container->values[index].size(), arg);
		if (ret != 0)
			return status::STOPPED_BY_CB;
		it++;
		index++;
	}

	return status::OK;
}

// [key, end), above or equal to key
status svector::get_equal_above(string_view key, get_kv_callback *callback, void *arg)
{
	LOG("get_equal_above start key>=" << std::string(key.data(), key.size()));
	check_outside_tx();
	auto it = std::lower_bound(container->keys.begin(), container->keys.end(),
				   key);
	size_t index = static_cast<size_t>(std::distance(container->keys.begin(), it));

	while (it != container->keys.end()) {
		auto ret = callback((*it).c_str(), (*it).size(),
				    container->values[index].c_str(),
				    container->values[index].size(), arg);
		if (ret != 0)
			return status::STOPPED_BY_CB;
		it++;
		index++;
	}

	return status::OK;
}

// [start, key], below or equal to key
status svector::get_equal_below(string_view key, get_kv_callback *callback, void *arg)
{
	LOG("get_equal_above start key>=" << std::string(key.data(), key.size()));
	check_outside_tx();
	auto it = container->keys.begin();
	size_t index = 0;

	while (it != container->keys.end() && !((*it) > key)) {
		auto ret = callback((*it).c_str(), (*it).size(),
				    container->values[index].c_str(), container->values[index].size(), arg);
		if (ret != 0)
			return status::STOPPED_BY_CB;
		it++;
		index++;
	}

	return status::OK;
}

// [start, key), less than key, key exclusive
status svector::get_below(string_view key, get_kv_callback *callback, void *arg)
{
	LOG("get_below key<" << std::string(key.data(), key.size()));
	check_outside_tx();
	auto it = container->keys.begin();
	size_t index = 0;

	while (it != container->keys.end() && ((*it) < key)) {
		auto ret = callback((*it).c_str(), (*it).size(),
				    container->values[index].c_str(), container->values[index].size(), arg);
		if (ret != 0)
			return status::STOPPED_BY_CB;
		it++;
		index++;
	}

	return status::OK;
}

status svector::exists(string_view key)
{
	LOG("exists for key=" << std::string(key.data(), key.size()));
	check_outside_tx();
	return binary_search(key) == -1 ? status::NOT_FOUND : status::OK;
}

status svector::get(string_view key, get_v_callback *callback, void *arg)
{
	LOG("get key=" << std::string(key.data(), key.size()));
	check_outside_tx();

	int index = binary_search(key);
	if (index == -1) {
		LOG("key not found");
		return status::NOT_FOUND;
	}
	assert(index >= 0);
	callback(container->values[static_cast<size_t>(index)].c_str(),
		 container->values[static_cast<size_t>(index)].size(), arg);
	return status::OK;
}

status svector::put(string_view key, string_view value)
{
	LOG("put key=" << std::string(key.data(), key.size())
		       << ", value.size=" << std::to_string(value.size()));
	check_outside_tx();

	auto it = std::lower_bound(container->keys.begin(), container->keys.end(),
				   key);
	size_t index = static_cast<size_t>(std::distance(container->keys.begin(), it));

	if(it != container->keys.end() && (*it) == key){
		// update value
		pmem::obj::transaction::run(pmpool, [&] {
		  container->values[index] = value;
		});
	} else {
		pmem::obj::transaction::run(pmpool, [&] {
		  container->keys.emplace(it, key);
		  container->values.emplace(container->values.cbegin() + index, value);
		});
	}

	return status::OK;
}

status svector::remove(string_view key)
{
	LOG("remove key=" << std::string(key.data(), key.size()));
	check_outside_tx();

	int index = binary_search(key);
	if (index == -1) {
		LOG("key not found");
		return status::NOT_FOUND;
	} else {
		// erase both value and key inside txn
		pmem::obj::transaction::run(pmpool, [&] {
		  container->keys.erase(container->keys.cbegin() + index);
		  container->values.erase(container->values.cbegin() + index);
		});
		return status::OK;
	}
}

void svector::Recover()
{
	if (!OID_IS_NULL(*root_oid)) {
		container = (internal::svector::kv_container *)pmemobj_direct(*root_oid);
	} else {
		pmem::obj::transaction::run(pmpool, [&] {
		  pmem::obj::transaction::snapshot(root_oid);
		  *root_oid =
			  pmem::obj::make_persistent<internal::svector::kv_container>().raw();
		  container = (internal::svector::kv_container *)pmemobj_direct(
			  *root_oid);
		});
	}
}

int svector::binary_search(string_view target)
{
	int  l = 0;
	int  r = container->keys.size() - 1;

	while (l <= r) {
		size_t m = l + (r - l) / 2;
		if (container->keys[m] == target)
			return m;
		// start the iteration
		if (container->keys[m] < target) {
			l = m + 1;
		} else {
			r = m - 1;
		}
	}
	// not present, return -1
	return -1;
}

} // namespace kv
} // namespace pmem
