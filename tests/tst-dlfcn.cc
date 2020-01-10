/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-dlfcn

#include <dlfcn.h>

#include <boost/test/unit_test.hpp>
namespace utf = boost::unit_test;

const bool rtld_next = false;
const bool deep_lookup = false;

static bool called = false;
extern "C" void DlSymTestFunction()
{
    called = true;
}

BOOST_AUTO_TEST_CASE(test_dlopen_with_null_as_filename)
{
    auto ref = dlopen(NULL, RTLD_NOW);
    BOOST_REQUIRE(ref != NULL);
    BOOST_REQUIRE(dlsym(ref, "open") != NULL);
    BOOST_REQUIRE(dlclose(ref) == 0);
}

BOOST_AUTO_TEST_CASE(test_dlopen_with_empty_file_name)
{
    auto ref = dlopen("", RTLD_NOW);
    BOOST_REQUIRE(ref != NULL);
    BOOST_REQUIRE(dlsym(ref, "open") != NULL);
    BOOST_REQUIRE(dlclose(ref) == 0);
}

template<typename T>
static void *adj_addr(T addr, int adj)
{
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(addr) + adj);
}

BOOST_AUTO_TEST_CASE(test_dladdr)
{
    Dl_info info;

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, -2), &info) != 0);
    BOOST_REQUIRE(!info.dli_sname || std::string("vfprintf") != std::string(info.dli_sname));
    BOOST_REQUIRE(vfprintf != info.dli_saddr);

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, 0), &info) != 0);
    BOOST_CHECK_EQUAL("vfprintf", info.dli_sname);
    BOOST_CHECK_EQUAL(vfprintf, info.dli_saddr);

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, 2), &info) != 0);
    BOOST_CHECK_EQUAL("vfprintf", info.dli_sname);
    BOOST_CHECK_EQUAL(vfprintf, info.dli_saddr);

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, 4), &info) != 0);
    BOOST_CHECK_EQUAL("vfprintf", info.dli_sname);
    BOOST_CHECK_EQUAL(vfprintf, info.dli_saddr);
}

BOOST_AUTO_TEST_CASE(test_dlsym_in_executable)
{
    dlerror();
    void* self = dlopen(NULL, RTLD_NOW);
    BOOST_REQUIRE_NE(self, nullptr);
    BOOST_REQUIRE_EQUAL(dlerror(), nullptr);

    void* sym = dlsym(self, "DlSymTestFunction");
    BOOST_REQUIRE_NE(sym, nullptr);

    void (*function)() = reinterpret_cast<void(*)()>(sym);

    called = false;
    function();
    BOOST_REQUIRE_EQUAL(called, true);
    BOOST_CHECK_EQUAL(0, dlclose(self));
}

BOOST_AUTO_TEST_CASE(test_dlsym_from_sofile,
        *utf::enable_if<rtld_next>())
{
    dlerror();
    void* handle = dlopen("/tests/libtest_dlsym_from_this.so", RTLD_LAZY | RTLD_LOCAL);
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE_NE(handle, nullptr);

    // check that we can't find '_test_dlsym_symbol' via dlsym(RTLD_DEFAULT)
    void* symbol = dlsym(RTLD_DEFAULT, "test_dlsym_symbol");
    (void)symbol;
    // TODO: dlopen ignores all the flags; the current implementation assumes the flag to be RTLD_GLOBAL but the default should be RTLD_LOCAL
    //       Should reenable the test after flags are implemented
    // BOOST_REQUIRE_EQUAL(symbol, nullptr);
    // auto err_msg = std::string(dlerror());
    // BOOST_TEST_CONTEXT(err_msg)
    // BOOST_REQUIRE_NE(err_msg.find("dlsym: symbol test_dlsym_symbol not found"),
    //     std::string::npos);
    // BOOST_REQUIRE_NE(err_msg.find("undefined symbol: test_dlsym_symbol"),
    //         std::string::npos);

    typedef int* (*fn_t)();
    fn_t lookup_dlsym_symbol_using_RTLD_DEFAULT =
        reinterpret_cast<fn_t>(dlsym(handle, "lookup_dlsym_symbol_using_RTLD_DEFAULT"));
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE_NE(lookup_dlsym_symbol_using_RTLD_DEFAULT, nullptr);

    int* ptr = lookup_dlsym_symbol_using_RTLD_DEFAULT();
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE_NE(ptr, nullptr);
    BOOST_REQUIRE_EQUAL(42, *ptr);

    fn_t lookup_dlsym_symbol2_using_RTLD_DEFAULT =
        reinterpret_cast<fn_t>(dlsym(handle, "lookup_dlsym_symbol2_using_RTLD_DEFAULT"));
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(lookup_dlsym_symbol2_using_RTLD_DEFAULT != nullptr);

    ptr = lookup_dlsym_symbol2_using_RTLD_DEFAULT();
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_REQUIRE_EQUAL(44, *ptr);

    fn_t lookup_dlsym_symbol_using_RTLD_NEXT =
        reinterpret_cast<fn_t>(dlsym(handle, "lookup_dlsym_symbol_using_RTLD_NEXT"));
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(lookup_dlsym_symbol_using_RTLD_NEXT != nullptr);

    ptr = lookup_dlsym_symbol_using_RTLD_NEXT();
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_REQUIRE_EQUAL(43, *ptr);

    dlclose(handle);
}

BOOST_AUTO_TEST_CASE(test_dlsym_from_sofile_with_preload,
        *utf::enable_if<deep_lookup>())
{
    void* preload = dlopen("/tests/libtest_dlsym_from_this_grandchild.so", RTLD_NOW | RTLD_LOCAL);
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(preload != nullptr);

    void* handle = dlopen("/tests/libtest_dlsym_from_this.so", RTLD_NOW | RTLD_LOCAL);
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(handle != nullptr);

    // check that we can't find '_test_dlsym_symbol' via dlsym(RTLD_DEFAULT)
    void* symbol = dlsym(RTLD_DEFAULT, "test_dlsym_symbol");
    (void)symbol;
    // TODO: dlopen ignores all the flags; the current implementation assumes the flag to be RTLD_GLOBAL but the default should be RTLD_LOCAL
    //       Should reenable the test after flags are implemented
    // BOOST_REQUIRE_EQUAL(symbol, nullptr);
    // auto err_msg = std::string(dlerror());
    // BOOST_TEST_CONTEXT(err_msg)
    // BOOST_REQUIRE_NE(err_msg.find("dlsym: symbol test_dlsym_symbol not found"),
    //      std::string::npos);
    // BOOST_REQUIRE_NE(err_msg.find("undefined symbol: test_dlsym_symbol"),
    //         std::string::npos);

    typedef int* (*fn_t)();
    fn_t lookup_dlsym_symbol_using_RTLD_DEFAULT =
        reinterpret_cast<fn_t>(dlsym(handle, "lookup_dlsym_symbol_using_RTLD_DEFAULT"));
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(lookup_dlsym_symbol_using_RTLD_DEFAULT != nullptr);

    int* ptr = lookup_dlsym_symbol_using_RTLD_DEFAULT();
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_REQUIRE_EQUAL(42, *ptr);

    fn_t lookup_dlsym_symbol2_using_RTLD_DEFAULT =
        reinterpret_cast<fn_t>(dlsym(handle, "lookup_dlsym_symbol2_using_RTLD_DEFAULT"));
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(lookup_dlsym_symbol2_using_RTLD_DEFAULT != nullptr);

    ptr = lookup_dlsym_symbol2_using_RTLD_DEFAULT();
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE_NE(ptr, nullptr);
    BOOST_REQUIRE_EQUAL(44, *ptr);

    fn_t lookup_dlsym_symbol_using_RTLD_NEXT =
        reinterpret_cast<fn_t>(dlsym(handle, "lookup_dlsym_symbol_using_RTLD_NEXT"));
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(lookup_dlsym_symbol_using_RTLD_NEXT != nullptr);

    ptr = lookup_dlsym_symbol_using_RTLD_NEXT();
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(ptr != nullptr);
    BOOST_REQUIRE_EQUAL(43, *ptr);

    dlclose(handle);
    dlclose(preload);
}

BOOST_AUTO_TEST_CASE(dlsym_handle_global_sym)
{
    // check that we do not look into global group
    // when looking up symbol by handle
    void* handle = dlopen("/tests/libtest_empty.so", RTLD_NOW);
    dlopen("libtest_with_dependency.so", RTLD_NOW | RTLD_GLOBAL);
    void* sym = dlsym(handle, "getRandomNumber");
    BOOST_REQUIRE(sym == nullptr);
    auto err_msg = std::string(dlerror());
    BOOST_TEST_CONTEXT(err_msg)
    BOOST_REQUIRE_NE(err_msg.find("dlsym: symbol getRandomNumber not found"),
            std::string::npos);
    // BOOST_REQUIRE_NE(err_msg.find("undefined symbol: getRandomNumber"),
    //         std::string::npos);

    sym = dlsym(handle, "DlSymTestFunction");
    BOOST_REQUIRE(sym == nullptr);
    err_msg = std::string(dlerror());
    BOOST_TEST_CONTEXT(err_msg)
    BOOST_REQUIRE_NE(err_msg.find("dlsym: symbol DlSymTestFunction not found"),
            std::string::npos);
    // BOOST_REQUIRE_NE(err_msg.find("undefined symbol: DlSymTestFunction"),
    //         std::string::npos);
    dlclose(handle);
}

BOOST_AUTO_TEST_CASE(dlsym_handle_empty_symbol)
{
    // check that dlsym of an empty symbol fails (see http://b/33530622)
    void* handle = dlopen("/tests/libtest_dlsym_from_this.so", RTLD_NOW);
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(handle != nullptr);
    void* sym = dlsym(handle, "");
    BOOST_REQUIRE(sym == nullptr);
    auto err_msg = std::string(dlerror());
    BOOST_TEST_CONTEXT(err_msg)
    BOOST_REQUIRE_NE(err_msg.find("dlsym: symbol  not found"),
            std::string::npos);
    // BOOST_REQUIRE_NE(err_msg.find("undefined symbol: "),
    //         std::string::npos);
    dlclose(handle);
}

BOOST_AUTO_TEST_CASE(dlsym_with_dependencies,
        *utf::enable_if<deep_lookup>())
{
    void* handle = dlopen("/tests/libtest_with_dependency.so", RTLD_NOW);
    BOOST_REQUIRE(handle != nullptr);
    dlerror();
    // This symbol is in DT_NEEDED library.
    void* sym = dlsym(handle, "getRandomNumber");
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE_NE(sym, nullptr);
    int (*fn)(void);
    fn = reinterpret_cast<int (*)(void)>(sym);
    BOOST_CHECK_EQUAL(4, fn());
    dlclose(handle);
}

BOOST_AUTO_TEST_CASE(rtld_default_unknown_symbol)
{
    void* addr = dlsym(RTLD_DEFAULT, "UNKNOWN");
    BOOST_REQUIRE_EQUAL(addr, nullptr);
}

BOOST_AUTO_TEST_CASE(rtld_default_fclose)
{
    void* addr = dlsym(RTLD_DEFAULT, "fclose");
    BOOST_REQUIRE_NE(addr, nullptr);
}

BOOST_AUTO_TEST_CASE(rtld_next_unknown_symbol,
        *utf::enable_if<rtld_next>())
{
    void* addr = dlsym(RTLD_NEXT, "UNKNOWN");
    BOOST_REQUIRE_EQUAL(addr, nullptr);
}

BOOST_AUTO_TEST_CASE(rtld_next_fclose,
        *utf::enable_if<rtld_next>())
{
    void* addr = dlsym(RTLD_NEXT, "fclose");
    BOOST_REQUIRE_NE(addr, nullptr);
}

BOOST_AUTO_TEST_CASE(rtld_next_from_lib, *utf::enable_if<rtld_next>())
{
    void* library_with_fclose = dlopen("/tests/libtest_check_rtld_next_from_library.so", RTLD_NOW | RTLD_GLOBAL);
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(library_with_fclose != nullptr);
    void* expected_addr = dlsym(RTLD_DEFAULT, "fclose");
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(expected_addr != nullptr);
    typedef void* (*get_libc_fclose_ptr_fn_t)();
    get_libc_fclose_ptr_fn_t get_libc_fclose_ptr =
        reinterpret_cast<get_libc_fclose_ptr_fn_t>(dlsym(library_with_fclose, "get_libc_fclose_ptr"));
    BOOST_TEST_CONTEXT(dlerror())
    BOOST_REQUIRE(get_libc_fclose_ptr != nullptr);
    BOOST_REQUIRE_EQUAL(expected_addr, get_libc_fclose_ptr());

    dlclose(library_with_fclose);
}
