/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INDEX_LIST_HH_
#define INDEX_LIST_HH_

// make_index_list<3> evaluates to index_list<0, 1, 2>.  This can be used when
// we want to enumerate a template argument pack, for example a tuple's.

template <size_t... N>
struct index_list {
};

template <size_t N, typename PrevIndexList>
struct glue_index_list;

template <size_t N, size_t... PrevIndices>
struct glue_index_list<N, index_list<PrevIndices...>> {
    using type = index_list<PrevIndices..., N>;
};

template <size_t N>
struct make_index_list_helper {
    using type = typename glue_index_list<N-1, typename make_index_list_helper<N-1>::type>::type;
};

template <>
struct make_index_list_helper<0> {
    using type = index_list<>;
};

template <size_t N>
using make_index_list = typename make_index_list_helper<N>::type;

#endif /* INDEX_LIST_HH_ */
