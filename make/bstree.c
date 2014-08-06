/*
 * Copyright (c) 2015 Jonas 'Sortie' Termansen.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * bstree.c
 * Binary search tree.
 */

// TODO: Clean up debug code prior to merging this.

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

//#include <stdio.h> // DEBUG.

#include "bstree.h"

static inline struct bstree* grandparent(struct bstree* node)
{
	return node->parent ? node->parent->parent : NULL;
}

static inline struct bstree* uncle(struct bstree* node)
{
	struct bstree* gnode = grandparent(node);
	if ( !gnode )
		return NULL;
	return node->parent == gnode->left ? gnode->right : gnode->left;
}

#if 0
static bool verify_tree(struct bstree* node, bool is_root)
{
	if ( !node )
		return true;
	if ( is_root && node->parent != NULL )
		return printf("(bad root parent)\n"), false;
	if ( node->left )
	{
		if ( node->left->parent != node )
			return printf("(bad left parent)\n"), false;
	}
	if ( node->right )
	{
		if ( node->right->parent != node )
			return printf("(bad right parent)\n"), false;
	}
	return verify_tree(node->left, false) &&
	       verify_tree(node->right, false);
}

static bool verify_search_tree(struct bstree* node,
                               int (*compare)(const void*, const void*))
{
	if ( !node )
		return true;
	if ( node->left && !(compare(node->left->value, node->value) < 0) )
		return false;
	if ( node->right && !(compare(node->value, node->right->value) < 0) )
		return false;
	return verify_search_tree(node->left, compare) &&
	       verify_search_tree(node->right, compare);
}
#endif

#if 0
static bool verify_red_black_tree(struct bstree* node)
{
	if ( !node )
		return true;
	if ( node->left )
	{
		if ( node->left->parent != node )
			return false;
	}
	if ( node->right )
	{
		if ( node->right->parent != node )
			return false;
	}
	return verify_red_black_tree(node->left) &&
	       verify_red_black_tree(node->right);
}
#endif

static inline void rotate_left(struct bstree* node, struct bstree** root_ptr)
{
	struct bstree* parent_node = node->parent;
	//assert(!parent_node || parent_node->left == node || parent_node->right == node);
	struct bstree* replacement_node = node->right;
	//assert(replacement_node);
	//assert(!node->left || node->left->parent == node);
	//assert(!node->right || node->right->parent == node);
	//assert(!replacement_node->left || replacement_node->left->parent == replacement_node);
	//assert(!replacement_node->right || replacement_node->right->parent == replacement_node);
	//assert(!parent_node || !parent_node->left || parent_node->left->parent == parent_node);
	//assert(!parent_node || !parent_node->right || parent_node->right->parent == parent_node);
	if ( parent_node && parent_node->left == node )
		parent_node->left = replacement_node;
	else if ( parent_node && parent_node->right == node )
		parent_node->right = replacement_node;
	else
		*root_ptr = replacement_node;
	replacement_node->parent = parent_node;
	node->right = replacement_node->left;
	if ( node->right )
		node->right->parent = node;
	replacement_node->left = node;
	node->parent = replacement_node;
	//assert(!node->left || node->left->parent == node);
	//assert(!node->right || node->right->parent == node);
	//assert(!replacement_node->left || replacement_node->left->parent == replacement_node);
	//assert(!replacement_node->right || replacement_node->right->parent == replacement_node);
	//assert(!parent_node || !parent_node->left || parent_node->left->parent == parent_node);
	//assert(!parent_node || !parent_node->right || parent_node->right->parent == parent_node);
}

static inline void rotate_right(struct bstree* node, struct bstree** root_ptr)
{
	struct bstree* parent_node = node->parent;
	struct bstree* replacement_node = node->left;
	//assert(replacement_node);
	if ( parent_node && parent_node->left == node )
		parent_node->left = replacement_node;
	else if ( parent_node && parent_node->right == node )
		parent_node->right = replacement_node;
	else
		*root_ptr = replacement_node;
	replacement_node->parent = parent_node;
	node->left = replacement_node->right;
	if ( node->left )
		node->left->parent = node;
	replacement_node->right = node;
	node->parent = replacement_node;
}

bool bstree_insert(struct bstree** tree_ptr,
                   void* value,
                   int (*compare)(const void*, const void*))
{
	struct bstree** root_ptr = tree_ptr;
#if 0
	assert(verify_tree(*root_ptr, true));
	assert(verify_search_tree(*root_ptr, compare));
#endif
	struct bstree* parent = NULL;
	struct bstree* tree = *tree_ptr;
	while ( tree )
	{
		int rel = compare(value, tree->value);
		assert(rel != 0);
		parent = tree;
		tree_ptr = rel < 0 ? &tree->left : &tree->right;
		tree = *tree_ptr;
	}
	struct bstree* child = (struct bstree*) calloc(1, sizeof(struct bstree));
	if ( !child )
		return NULL;
	child->parent = parent;
	child->value = value;
	child->red = true;
	*tree_ptr = child;
#if 0
	assert(verify_tree(*root_ptr, true));
	assert(verify_search_tree(*root_ptr, compare));
#endif
	struct bstree* node = child;
	while ( node )
	{
		if ( !node->parent )
		{
			node->red = false;
			break;
		}
		if ( !node->parent->red )
			break;
		struct bstree* unode = uncle(node);
		struct bstree* gnode = grandparent(node);
		//assert(gnode);
		if ( unode && unode->red )
		{
			node->parent->red = false;
			unode->red = false;
			gnode->red = true;
			node = gnode;
			continue;
		}
		if ( node == node->parent->right && node->parent == gnode->left )
		{
			//assert(verify_tree(*root_ptr, true));
			//assert(verify_search_tree(*root_ptr, compare));
			rotate_left(node->parent, root_ptr);
			//assert(verify_search_tree(*root_ptr, compare));
			//assert(verify_tree(*root_ptr, true));
			node = node->left;
			gnode = grandparent(node);
			//assert(gnode);
		}
		else if ( node == node->parent->left && node->parent == gnode->right )
		{
			//assert(verify_tree(*root_ptr, true));
			//assert(verify_search_tree(*root_ptr, compare));
			rotate_right(node->parent, root_ptr);
			//assert(verify_tree(*root_ptr, true));
			//assert(verify_search_tree(*root_ptr, compare));
			node = node->right;
			gnode = grandparent(node);
			//assert(gnode);
		}
		node->parent->red = false;
		gnode->red = true;
		if ( node == node->parent->left )
		{
			//assert(verify_tree(*root_ptr, true));
			//assert(verify_search_tree(*root_ptr, compare));
			rotate_right(gnode, root_ptr);
			//assert(verify_tree(*root_ptr, true));
			//assert(verify_search_tree(*root_ptr, compare));
		}
		else
		{
			//assert(verify_tree(*root_ptr, true));
			//assert(verify_search_tree(*root_ptr, compare));
			rotate_left(gnode, root_ptr);
			//assert(verify_tree(*root_ptr, true));
			//assert(verify_search_tree(*root_ptr, compare));
		}
		break;
	}
#if 0
	assert(verify_tree(*root_ptr, true));
	assert(verify_search_tree(*root_ptr, compare));
#endif
	//assert(verify_red_black_tree(*root_ptr));
	return true;
}

void* bstree_search(struct bstree* tree,
                    const void* key,
                    int (*search)(const void*, const void*))
{
	if ( !tree )
		return NULL;
	int rel = search(key, tree->value);
	if ( rel == 0 )
		return tree->value;
	return bstree_search(rel < 0 ? tree->left : tree->right, key, search);
}
