/*
 * Copyright 2017-2020, Intel Corporation
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


#ifndef LIBPMEMKV_H
#define LIBPMEMKV_H

#include <stddef.h>
#include <stdint.h>
#include <string>

#if __cpp_lib_string_view
#include <string_view>
#endif

namespace pmem
{
namespace kv
{
#if __cpp_lib_string_view
using string_view = std::string_view;
#else
/*! \class string_view
        \brief Our brief std::string_view implementation.

        If C++17's std::string_view implementation is not available, this one is used
        to avoid unnecessary string copying.
*/
class string_view {
public:
        string_view() noexcept;
        string_view(const char *data, size_t size);
        string_view(const std::string &s);
        string_view(const char *data);

        string_view(const string_view &rhs) noexcept = default;
        string_view &operator=(const string_view &rhs) noexcept = default;

        const char *data() const noexcept;
        std::size_t size() const noexcept;

        int compare(const string_view &other) noexcept;

private:
        const char *_data;
        std::size_t _size;
};

/**
 * Default constructor with empty data.
 */
inline string_view::string_view() noexcept : _data(""), _size(0)
{
}

/**
 * Constructor initialized by *data* and its *size*.
 *
 * @param[in] data pointer to the C-like string (char *) to initialize with,
 *				it can contain null characters
 * @param[in] size length of the given data
 */
inline string_view::string_view(const char *data, size_t size) : _data(data), _size(size)
{
}

/**
 * Constructor initialized by the string *s*.
 *
 * @param[in] s reference to the string to initialize with
 */
inline string_view::string_view(const std::string &s) : _data(s.c_str()), _size(s.size())
{
}

/**
 * Constructor initialized by *data*. Size of the data will be set
 * using std::char_traits<char>::length().
 *
 * @param[in] data pointer to C-like string (char *) to initialize with,
 *				it has to end with the terminating null character
 */
inline string_view::string_view(const char *data)
    : _data(data), _size(std::char_traits<char>::length(data))
{
}

/**
 * Returns pointer to data stored in this pmem::kv::string_view. It may not contain
 * the terminating null character.
 *
 * @return pointer to C-like string (char *), it may not end with null character
 */
inline const char *string_view::data() const noexcept
{
	return _data;
}

/**
 * Returns count of characters stored in this pmem::kv::string_view data.
 *
 * @return pointer to C-like string (char *), it may not end with null character
 */
inline std::size_t string_view::size() const noexcept
{
	return _size;
}

/**
 * Compares this string_view with other. Works in the same way as
 * std::basic_string::compare.
 *
 * @return 0 if both character sequences compare equal,
 *			positive value if this is lexicographically greater than other,
 *			negative value if this is lexicographically less than other.
 */
inline int string_view::compare(const string_view &other) noexcept
{
	int ret = std::char_traits<char>::compare(data(), other.data(),
						  std::min(size(), other.size()));
	if (ret != 0)
		return ret;
	if (size() < other.size())
		return -1;
	if (size() > other.size())
		return 1;
	return 0;
}
#endif

}
}

#ifdef __cplusplus

extern "C" {
#endif

#define PMEMKV_STATUS_OK 0
#define PMEMKV_STATUS_UNKNOWN_ERROR 1
#define PMEMKV_STATUS_NOT_FOUND 2
#define PMEMKV_STATUS_NOT_SUPPORTED 3
#define PMEMKV_STATUS_INVALID_ARGUMENT 4
#define PMEMKV_STATUS_CONFIG_PARSING_ERROR 5
#define PMEMKV_STATUS_CONFIG_TYPE_ERROR 6
#define PMEMKV_STATUS_STOPPED_BY_CB 7
#define PMEMKV_STATUS_OUT_OF_MEMORY 8
#define PMEMKV_STATUS_WRONG_ENGINE_NAME 9
#define PMEMKV_STATUS_TRANSACTION_SCOPE_ERROR 10
#define PMEMKV_STATUS_DEFRAG_ERROR 11

typedef struct pmemkv_db pmemkv_db;
typedef struct pmemkv_config pmemkv_config;

typedef int pmemkv_get_kv_callback(const char *key, size_t keybytes, const char *value,
				   size_t valuebytes, void *arg);
typedef void pmemkv_get_v_callback(const char *value, size_t valuebytes, void *arg);

pmemkv_config *pmemkv_config_new(void);
void pmemkv_config_delete(pmemkv_config *config);
int pmemkv_config_put_data(pmemkv_config *config, const char *key, const void *value,
			   size_t value_size);
int pmemkv_config_put_object(pmemkv_config *config, const char *key, void *value,
			     void (*deleter)(void *));
int pmemkv_config_put_uint64(pmemkv_config *config, const char *key, uint64_t value);
int pmemkv_config_put_int64(pmemkv_config *config, const char *key, int64_t value);
int pmemkv_config_put_string(pmemkv_config *config, const char *key, const char *value);
int pmemkv_config_get_data(pmemkv_config *config, const char *key, const void **value,
			   size_t *value_size);
int pmemkv_config_get_object(pmemkv_config *config, const char *key, void **value);
int pmemkv_config_get_uint64(pmemkv_config *config, const char *key, uint64_t *value);
int pmemkv_config_get_int64(pmemkv_config *config, const char *key, int64_t *value);
int pmemkv_config_get_string(pmemkv_config *config, const char *key, const char **value);

int pmemkv_open(const char *engine, pmemkv_config *config, pmemkv_db **db);
void pmemkv_close(pmemkv_db *kv);

int pmemkv_count_all(pmemkv_db *db, size_t *cnt);
int pmemkv_count_above(pmemkv_db *db, const char *k, size_t kb, size_t *cnt);
int pmemkv_count_equal_above(pmemkv_db *db, const char *k, size_t kb, size_t *cnt);
int pmemkv_count_equal_below(pmemkv_db *db, const char *k, size_t kb, size_t *cnt);
int pmemkv_count_below(pmemkv_db *db, const char *k, size_t kb, size_t *cnt);
int pmemkv_count_between(pmemkv_db *db, const char *k1, size_t kb1, const char *k2,
			 size_t kb2, size_t *cnt);

int pmemkv_get_all(pmemkv_db *db, pmemkv_get_kv_callback *c, void *arg);
int pmemkv_get_above(pmemkv_db *db, const char *k, size_t kb, pmemkv_get_kv_callback *c,
		     void *arg);
int pmemkv_get_equal_above(pmemkv_db *db, const char *k, size_t kb,
			   pmemkv_get_kv_callback *c, void *arg);
int pmemkv_get_equal_below(pmemkv_db *db, const char *k, size_t kb,
			   pmemkv_get_kv_callback *c, void *arg);
int pmemkv_get_below(pmemkv_db *db, const char *k, size_t kb, pmemkv_get_kv_callback *c,
		     void *arg);
int pmemkv_get_between(pmemkv_db *db, const char *k1, size_t kb1, const char *k2,
		       size_t kb2, pmemkv_get_kv_callback *c, void *arg);

int pmemkv_exists(pmemkv_db *db, const char *k, size_t kb);

std::pair<pmem::kv::string_view, pmem::kv::string_view> pmemkv_upper_bound(struct pmemkv_db *db, pmem::kv::string_view k);
std::pair<pmem::kv::string_view, pmem::kv::string_view> pmemkv_lower_bound(struct pmemkv_db *db, pmem::kv::string_view k);
std::pair<pmem::kv::string_view, pmem::kv::string_view> pmemkv_get_begin(struct pmemkv_db *db);
std::pair<pmem::kv::string_view, pmem::kv::string_view> pmemkv_get_next(struct pmemkv_db *db, pmem::kv::string_view k);
std::pair<pmem::kv::string_view, pmem::kv::string_view> pmemkv_get_prev(struct pmemkv_db *db, pmem::kv::string_view k);
int pmemkv_get_size_new(struct pmemkv_db *db);

int pmemkv_get(pmemkv_db *db, const char *k, size_t kb, pmemkv_get_v_callback *c,
	       void *arg);
int pmemkv_get_copy(pmemkv_db *db, const char *k, size_t kb, char *buffer,
		    size_t buffer_size, size_t *value_size);
int pmemkv_put(pmemkv_db *db, const char *k, size_t kb, const char *v, size_t vb);

int pmemkv_remove(pmemkv_db *db, const char *k, size_t kb);

int pmemkv_defrag(pmemkv_db *db, double start_percent, double amount_percent);

const char *pmemkv_errormsg(void);

#ifdef __cplusplus
} /* end extern "C" */

#endif

#endif /* LIBPMEMKV_H */

