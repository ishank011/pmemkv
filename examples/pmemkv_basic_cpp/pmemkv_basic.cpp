/*
 * Copyright 2019-2020, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * pmemkv_basic.cpp -- example usage of pmemkv.
 */

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <libpmemkv.hpp>

#define LOG(msg) std::cout << msg << std::endl

using namespace pmem::kv;

const uint64_t SIZE = 1024UL * 1024UL * 1024UL;

int f1(string_view k, string_view v) {
                LOG(" Key visited: " << k.data());
                LOG(" Value visited: " << v.data());
                return 0;
}

int f2(string_view k, string_view v) {
                LOG(" Hey Key visited: " << k.data());
                LOG(" Hey Value visited: " << v.data());
                return 0;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " file\n";
		exit(1);
	}

	/* See libpmemkv_config(3) for more detailed example of config creation */
	LOG("Creating config");
	config cfg;

	status s = cfg.put_string("path", argv[1]);
	assert(s == status::OK);
	s = cfg.put_uint64("size", SIZE);
	assert(s == status::OK);
	s = cfg.put_uint64("force_create", 1);
	assert(s == status::OK);

	LOG("Opening pmemkv database with 'cmap' engine");
	db *kv = new db();
	assert(kv != nullptr);
	s = kv->open("stree", std::move(cfg));
	assert(s == status::OK);

	LOG("Putting new key");
	s = kv->put("key1", "value1");
	assert(s == status::OK);

	size_t cnt;
	s = kv->count_all(cnt);
	assert(s == status::OK && cnt == 1);

	LOG("Reading key back");
	std::string value;
	s = kv->get("key1", &value);
	assert(s == status::OK && value == "value1");

	LOG("Iterating existing keys");
	s = kv->put("key2", "value2");
	kv->put("key3", "value3");
	kv->put("key4", "value4");
	kv->put("key5", "value5");
	kv->put("key6", "value6");
	kv->put("key7", "value7");
	kv->put("key8", "value8");
	kv->put("key9", "value9");
	kv->put("key10", "value10");
	kv->put("key11", "value11");
	kv->put("key12", "value12");
	kv->put("key13", "value13");
	kv->put("key14", "value14");
	kv->put("key15", "value15");
	kv->put("key16", "value16");
	kv->put("key17", "value17");
	kv->put("key18", "value18");
	
	//kv->get_all(f1);
	kv->get_all([](string_view k, string_view v) {
		LOG(" Key visited: " << k.data());
		LOG(" Value visited: " << v.data());
		return 0;
	});
	/*
	kv->get_above("key5", f2);

	s = kv->upper_bound("key9", &value);
	assert(s == status::NOT_FOUND);
	std::cout << value;

	s = kv->upper_bound("key12", &value);
	assert(value == "key13");
	*/
	/*s = kv->get_next("key15", &value);
	assert(value == "key16");*/
	
	std::pair<string_view, string_view> a = kv->upper_bound("key6");
	std::cout << a.first.data() << " " << a.second.data();

	std::pair<string_view, string_view> b = kv->lower_bound("key9");
	std::cout << b.first.data() << " " << b.second.data();

	std::pair<string_view, string_view> c = kv->get_begin();
	std::cout << c.first.data() << " " << c.second.data();

	LOG("Removing existing key");
	s = kv->remove("key1");
	assert(s == status::OK);
	s = kv->exists("key1");
	assert(s == status::NOT_FOUND);

	kv->get_all(f1);

	LOG("Closing database");
	delete kv;

	return 0;
}
