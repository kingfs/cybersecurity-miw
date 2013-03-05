/* Metis
 * Yandong Mao, Robert Morris, Frans Kaashoek
 * Copyright (c) 2012 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, subject to the conditions listed
 * in the Metis LICENSE file. These conditions include: you must preserve this
 * copyright notice, and you cannot mention the copyright holders in
 * advertising related to the Software without their permission.  The Software
 * is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This notice is a
 * summary of the Metis LICENSE file; the license in that file is legally
 * binding.
 */
#ifndef BTREE_HH_
#define BTREE_HH_ 1

#include "bsearch.hh"
#include "appbase.hh"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

enum { order = 3 };

template <typename PAIR>
struct btnode_internal;

template <typename PAIR>
struct btnode_base {
    btnode_internal<PAIR> *parent_;
    short nk_;
    btnode_base() : parent_(NULL), nk_(0) {}
    virtual ~btnode_base() {}
};

template <typename PAIR>
struct btnode_leaf : public btnode_base<PAIR> {
    static const int fanout = 2 * order + 2;
    typedef btnode_leaf<PAIR> self_type;
    typedef decltype(((PAIR *)0)->key_) key_type;
    typedef btnode_base<PAIR> base_type;
    using base_type::nk_;

    PAIR e_[fanout];
    self_type *next_;
    ~btnode_leaf() {
        for (int i = 0; i < nk_; ++i)
            e_[i].reset();
        for (int i = nk_; i < fanout; ++i)
            e_[i].init();
    }

    btnode_leaf() : base_type(), next_(NULL) {
        for (int i = 0; i < fanout; ++i)
            e_[i].init();
    }
    self_type *split() {
        auto right = new self_type;
        memcpy(right->e_, &e_[order + 1], sizeof(e_[0]) * (1 + order));
        right->nk_ = order + 1;
        nk_ = order + 1;
        auto next = next_;
        next_ = right;
        right->next_ = next;
        return right;
    }

    bool lower_bound(const key_type &key, int *p) {
        bool found = false;
        PAIR tmp;
        tmp.key_ = key;
        *p = xsearch::lower_bound(&tmp, e_, nk_,
                                  static_appbase::pair_comp<PAIR>, &found);
        return found;
    }

    void insert(int pos, const key_type &key, unsigned hash) {
        if (pos < nk_)
            memmove(&e_[pos + 1], &e_[pos], sizeof(e_[0]) * (nk_ - pos));
        ++ nk_;
        e_[pos].init();
        e_[pos].key_ = key;
        e_[pos].hash = hash;
    }

    bool need_split() const {
        return nk_ == fanout;
    }
};

template <typename PAIR>
struct btnode_internal : public btnode_base<PAIR> {
    static const int fanout = 2 * order + 2;
    typedef btnode_internal<PAIR> self_type;
    typedef decltype(((PAIR *)0)->key_) key_type;
    typedef btnode_base<PAIR> base_type;
    using base_type::nk_;

    struct xpair {
        xpair(const key_type &k) : key_(k) {} 
        xpair() : key_(), v_() {} 
        key_type key_;
        base_type *v_;
    };

    xpair e_[fanout];
    btnode_internal() {
        bzero(e_, sizeof(e_));
    }
    virtual ~btnode_internal() {}

    self_type *split() {
        auto nn = new self_type;
        nn->nk_ = order;
        memcpy(nn->e_, &e_[order + 1], sizeof(e_[0]) * (order + 1));
        nk_ = order;
        return nn;
    }
    void assign(int p, base_type *left, const key_type &key, base_type *right) {
        e_[p].v_ = left;
        e_[p].key_ = key;
        e_[p + 1].v_ = right;
    }
    void assign_right(int p, const key_type &key, base_type *right) {
        e_[p].key_ = key;
        e_[p + 1].v_ = right;
    }
    base_type *upper_bound(const key_type &key) {
        int pos = upper_bound_pos(key);
        return e_[pos].v_;
    }
    int upper_bound_pos(const key_type &key) {
        xpair tmp(key);
        return xsearch::upper_bound(&tmp, e_, nk_, static_appbase::pair_comp<xpair>);
    }
    bool need_split() const {
        return nk_ == fanout - 1;
    }
};


template <typename PAIR>
struct btree_type {
    typedef PAIR element_type;
    typedef btnode_leaf<PAIR> leaf_node_type;
    typedef typename btnode_leaf<PAIR>::key_type key_type;
    typedef btnode_internal<PAIR> internal_node_type;
    typedef btnode_base<PAIR> base_node_type;

    void init();
    /* @brief: free the tree, but not the values */
    void shallow_free();
    void map_insert_sorted_new_and_raw(PAIR *kvs);

    /* @brief: insert key/val pair into the tree
       @return true if it is a new key */
    int map_insert_sorted_copy_on_new(const key_type &key, void *val, size_t keylen, unsigned hash);
    size_t size() const;
    uint64_t transfer(xarray<PAIR> *dst);
    uint64_t copy(xarray<PAIR> *dst);

    /* @brief: return the number of values in the tree */
    uint64_t test_get_nvalue() {
        iterator i = begin();
        uint64_t n = 0;
        while (i != end()) {
            n += i->size();
            ++ i;
        }
        return n;
    }

    struct iterator {
        iterator() : c_(NULL), i_(0) {}
        explicit iterator(leaf_node_type *c) : c_(c), i_(0) {}
        iterator &operator=(const iterator &a) {
            c_ = a.c_;
            i_ = a.i_;
            return *this;
        }
        void operator++() {
            if (c_ && i_ + 1 == c_->nk_) {
                c_ = c_->next_;
                i_ = 0;
            } else if (c_)
                ++i_;
            else
                assert(0);
        }
        void operator++(int) {
            ++(*this);
        }
        bool operator==(const iterator &a) {
            return (!c_ && !a.c_) || (c_ == a.c_ && i_ == a.i_);
        }
        bool operator!=(const iterator &a) {
            return !(*this == a);
        }
        PAIR *operator->() {
            return &c_->e_[i_];
        }
        PAIR &operator*() {
            return c_->e_[i_];
        }
      private:
        leaf_node_type *c_;
        int i_;
    };

    iterator begin();
    iterator end();

  private:
    size_t nk_;
    short nlevel_;
    base_node_type *root_;
    uint64_t copy_traverse(xarray<PAIR> *dst, bool clear_leaf);

    /* @brief: insert @key at position @pos into leaf node @leaf,
     * and set the value of that key to empty */
    static void insert_leaf(leaf_node_type *leaf, const key_type &key, int pos, int keylen);
    static void delete_level(base_node_type *node, int level);

    leaf_node_type *first_leaf() const;

    /* @brief: insert (@key, @right) into left's parent */
    void insert_internal(const key_type &key, base_node_type *left, base_node_type *right);
    leaf_node_type *get_leaf(const key_type &key);
};

template <typename PAIR>
void btree_type<PAIR>::init() {
    nk_ = 0;
    nlevel_ = 0;
    root_ = NULL;
}

// left < key <= right. Right is the new sibling
template <typename PAIR>
void btree_type<PAIR>::insert_internal(const key_type &key, base_node_type *left, base_node_type *right) {
    auto parent = left->parent_;
    if (!parent) {
	auto newroot = new internal_node_type;
	newroot->nk_ = 1;
        newroot->assign(0, left, key, right);
	root_ = newroot;
	left->parent_ = newroot;
	right->parent_ = newroot;
	++nlevel_;
    } else {
	int ikey = parent->upper_bound_pos(key);
	// insert newkey at ikey, values at ikey + 1
	for (int i = parent->nk_ - 1; i >= ikey; i--)
	    parent->e_[i + 1].key_ = parent->e_[i].key_;
	for (int i = parent->nk_; i >= ikey + 1; i--)
	    parent->e_[i + 1].v_ = parent->e_[i].v_;
        parent->assign_right(ikey, key, right);
	++parent->nk_;
	right->parent_ = parent;
	if (parent->need_split()) {
	    key_type newkey = parent->e_[order].key_;
	    auto newparent = parent->split();
	    // push up newkey
	    insert_internal(newkey, parent, newparent);
	    // fix parent pointers
	    for (int i = 0; i < newparent->nk_ + 1; ++i)
		newparent->e_[i].v_->parent_ = newparent;
	}
    }
}

template <typename PAIR>
btnode_leaf<PAIR> *btree_type<PAIR>::get_leaf(const key_type &key) {
    if (!nlevel_) {
	root_ = new leaf_node_type;
	nlevel_ = 1;
	nk_ = 0;
	return static_cast<leaf_node_type *>(root_);
    }
    auto node = root_;
    for (int i = 0; i < nlevel_ - 1; ++i)
        node = static_cast<internal_node_type *>(node)->upper_bound(key);
    return static_cast<leaf_node_type *>(node);
}

// left < splitkey <= right. Right is the new sibling
template <typename PAIR>
int btree_type<PAIR>::map_insert_sorted_copy_on_new(const key_type &k, void *v, size_t keylen, unsigned hash) {
    auto leaf = get_leaf(k);
    int pos;
    bool found;
    if (!(found = leaf->lower_bound(k, &pos))) {
        void *ik = static_appbase::key_copy(k, keylen);
        leaf->insert(pos, ik, hash);
        ++ nk_;
    }
    leaf->e_[pos].map_value_insert(v);
    if (leaf->need_split()) {
	auto right = leaf->split();
        insert_internal(right->e_[0].key_, leaf, right);
    }
    return !found;
}

template <typename PAIR>
void btree_type<PAIR>::map_insert_sorted_new_and_raw(PAIR *p) {
    auto leaf = get_leaf(p->key_);
    int pos;
    assert(!leaf->lower_bound(p->key_, &pos));  // must be new key
    leaf->insert(pos, p->key_, 0);  // do not copy key
    ++ nk_;
    leaf->e_[pos] = *p;
    if (leaf->need_split()) {
        auto right = leaf->split();
        insert_internal(right->e_[0].key_, leaf, right);
    }
}

template <typename PAIR>
size_t btree_type<PAIR>::size() const {
    return nk_;
}

template <typename PAIR>
void btree_type<PAIR>::delete_level(base_node_type *node, int level) {
    for (int i = 0; level > 1 && i <= node->nk_; ++i)
        delete_level(static_cast<internal_node_type *>(node)->e_[i].v_, level - 1);
    delete node;
}

template <typename PAIR>
void btree_type<PAIR>::shallow_free() {
    if (!nlevel_)
        return;
    delete_level(root_, nlevel_);
    init();
}

template <typename PAIR>
typename btree_type<PAIR>::iterator btree_type<PAIR>::begin() {
    return iterator(first_leaf());
}

template <typename PAIR>
typename btree_type<PAIR>::iterator btree_type<PAIR>::end() {
    return btree_type<PAIR>::iterator(NULL);
}

template <typename PAIR>
uint64_t btree_type<PAIR>::copy(xarray<PAIR> *dst) {
    return copy_traverse(dst, false);
}

template <typename PAIR>
uint64_t btree_type<PAIR>::transfer(xarray<PAIR> *dst) {
    uint64_t n = copy_traverse(dst, true);
    shallow_free();
    return n;
}

template <typename PAIR>
uint64_t btree_type<PAIR>::copy_traverse(xarray<PAIR> *dst, bool clear_leaf) {
    assert(dst->size() == 0);
    if (!nlevel_)
	return 0;
    dst->resize(size());
    auto leaf = first_leaf();
    uint64_t n = 0;
    while (leaf) {
	memcpy(dst->at(n), leaf->e_, sizeof(PAIR) * leaf->nk_);
	n += leaf->nk_;
        if (clear_leaf)
            leaf->nk_ = 0;  // quickly delete all key/values from the leaf
        leaf = leaf->next_;
    }
    assert(n == nk_);
    return n;
}

template <typename PAIR>
btnode_leaf<PAIR> *btree_type<PAIR>::first_leaf() const {
    if (!nk_)
        return NULL;
    auto node = root_;
    for (int i = 0; i < nlevel_ - 1; ++i)
	node = static_cast<internal_node_type *>(node)->e_[0].v_;
    return static_cast<leaf_node_type *>(node);
}
#endif
