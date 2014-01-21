/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_RUN_H
#define INCLUDED_OSV_RUN_H

#include <osv/elf.hh>
#include <string>
#include <vector>


/**
 * OSv namespace
 */
namespace osv {

/** @name #include <osv/run.hh>
 * Convenience functions for running shared-object executables
 */
/**@{*/

/**
 * Run the given executable.
 *
 * Currently, The executable is assumed to be a shared object with a \c main()
 * function as in Unix (i.e., a \c main function with C linkage, taking argc
 * and argv and returning an int).
 * This \c main function is run in the current thread.
 *
 * run() loads the given shared object into memory, and unloads it if the caller
 * does not save the returned shared pointer.
 * In particular, if the same object is run concurrently, it will only be
 * unloaded after the last of the concurrent runs has finished.
 *
 * It is worth noting how run() of a shared object differs Unix's traditional
 * exec() of an executable:
 *
 * \li run() runs the \c main function in the current thread, but other treads
 *     continue to run normally. run() returns when the \c main function
 *     returns.
 * \li When a Unix executable exits, its resources are all cleaned up. But our
 *     shared objects need to clean up after themselves. If main() completes
 *     with memory allocated, with open files, etc., then these resources will
 *     never be freed. If main() returns while threads are still running its
 *     code, or pointers to its memory are somewhere in the kernel's
 *     structures, a crash will likely soon follow.
 *     A convenient way to ensure that certain cleanups happen when the object
 *     is unloaded is to use static objects with destructors. These
 *     destructors are guaranteed to be run when the object is unloaded.
 *     To run a main() which returns without waiting for all its threads to
 *     finish, you must use elf::program::get_library() directly.
 * \li When several instances of the same object run concurrently, they share
 *     a single copy of static and global variables. Therefore main() cannot
 *     assume that when it starts, static variables have their default values.
 *     Moreover, a shared-object which is to be run concurrently on multiple
 *     threads needs to access its global variables in a thread-safe manner
 *     (e.g., using mutexes), or avoid globals, even if each main() call itself
 *     is single-threaded. Some non-reentrant library functions (such as
 *     getopt() with its \c optind global) are especially problematic in this
 *     regard.
 * \li The main() must complete with "return", not with exit().
 *
 * \param[in]  path        The pathname of the shared object to run
 * \param[in]  argc        The length of the argument array <TT>argv</TT> .
 * \param[in]  argv        The arguments to the object's main function.
 *                         Traditionally, <TT>argv[0]</TT> is the name of the
 *                         program, often same as <TT>object</TT>.
 * \param[out] return_code Where to write main()'s return code. If
 *                         <TT>return_code == nulltr</TT>, main()'s return
 *                         code is ignored.
 *
 * \return \c shared pointer to the library if run, \c empty shared pointer if
 *                         couldn't run
 */
std::shared_ptr<elf::object> run(std::string path,
                                 int argc, char** argv, int *return_code);

/**
 * Run the given executable.
 *
 * Currently, The executable is assumed to be a shared object with a \c main()
 * function as in Unix (i.e., a \c main function with C linkage, taking argc
 * and argv and returning an int).
 * This \c main function is run in the current thread.
 *
 * run() loads the given shared object into memory, and unloads it if the caller
 * does not save the returned shared pointer.
 * In particular, if the same object is run concurrently, it will only be
 * unloaded after the last of the concurrent runs has finished.
 *
 * It is worth noting how run() of a shared object differs Unix's traditional
 * exec() of an executable:
 *
 * \li run() runs the \c main function in the current thread, but other treads
 *     continue to run normally. run() returns when the \c main function
 *     returns.
 * \li When a Unix executable exits, its resources are all cleaned up. But our
 *     shared objects need to clean up after themselves. If main() completes
 *     with memory allocated, with open files, etc., then these resources will
 *     never be freed. If main() returns while threads are still running its
 *     code, or pointers to its memory are somewhere in the kernel's
 *     structures, a crash will likely soon follow.
 *     A convenient way to ensure that certain cleanups happen when the object
 *     is unloaded is to use static objects with destructors. These
 *     destructors are guaranteed to be run when the object is unloaded.
 *     To run a main() which returns without waiting for all its threads to
 *     finish, you must use elf::program::get_library() directly.
 * \li When several instances of the same object run concurrently, they share
 *     a single copy of static and global variables. Therefore main() cannot
 *     assume that when it starts, static variables have their default values.
 *     Moreover, a shared-object which is to be run concurrently on multiple
 *     threads needs to access its global variables in a thread-safe manner
 *     (e.g., using mutexes), or avoid globals, even if each main() call itself
 *     is single-threaded. Some non-reentrant library functions (such as
 *     getopt() with its \c optind global) are especially problematic in this
 *     regard.
 * \li The main() must complete with "return", not with exit().
 *
 * \param[in]  path        The pathname of the shared object to run
 * \param[in]  args        The argument array <TT>argv</TT>. Traditionally,
 *                         <TT>args[0]</TT> is the name of the
 *                         program, often same as <TT>object</TT>.
 * \param[out] return_code Where to write main()'s return code. If
 *                         <TT>return_code == nulltr</TT>, main()'s return
 *                         code is ignored.
 *
 * \return \c shared pointer to the library if run, \c empty shared pointer
 *                          if couldn't run
 */
std::shared_ptr<elf::object> run(std::string path,
                                 std::vector<std::string> args,
                                 int* return_code);

/**@}*/

}
#endif /* INCLUDED_OSV_RUN_H */
