/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#pragma once

#include "FN_multi_function.hh"

#include "DNA_node_types.h"

#include "NOD_derived_node_tree.hh"

namespace blender::nodes {

using namespace fn::multi_function_types;

class NodeMultiFunctions;

/**
 * Utility class to help nodes build a multi-function for themselves.
 */
class NodeMultiFunctionBuilder : NonCopyable, NonMovable {
 private:
  ResourceScope &resource_scope_;
  bNode &node_;
  bNodeTree &tree_;
  const MultiFunction *built_fn_ = nullptr;

  friend NodeMultiFunctions;

 public:
  NodeMultiFunctionBuilder(ResourceScope &resource_scope, bNode &node, bNodeTree &tree);

  /**
   * Assign a multi-function for the current node. The input and output parameters of the function
   * have to match the available sockets in the node.
   */
  void set_matching_fn(const MultiFunction *fn);
  void set_matching_fn(const MultiFunction &fn);

  /**
   * Utility method for creating and assigning a multi-function when it can't have a static
   * lifetime.
   */
  template<typename T, typename... Args> void construct_and_set_matching_fn(Args &&...args);

  bNode &node();
  bNodeTree &tree();

  ResourceScope &resource_scope();
};

/**
 * Gives access to multi-functions for all nodes in a node tree that support them.
 */
class NodeMultiFunctions {
 private:
  Map<const bNode *, const MultiFunction *> map_;

 public:
  NodeMultiFunctions(const DerivedNodeTree &tree, ResourceScope &resource_scope);

  const MultiFunction *try_get(const DNode &node) const;
};

/* -------------------------------------------------------------------- */
/** \name #NodeMultiFunctionBuilder Inline Methods
 * \{ */

inline NodeMultiFunctionBuilder::NodeMultiFunctionBuilder(ResourceScope &resource_scope,
                                                          bNode &node,
                                                          bNodeTree &tree)
    : resource_scope_(resource_scope), node_(node), tree_(tree)
{
}

inline bNode &NodeMultiFunctionBuilder::node()
{
  return node_;
}

inline bNodeTree &NodeMultiFunctionBuilder::tree()
{
  return tree_;
}

inline ResourceScope &NodeMultiFunctionBuilder::resource_scope()
{
  return resource_scope_;
}

inline void NodeMultiFunctionBuilder::set_matching_fn(const MultiFunction *fn)
{
  built_fn_ = fn;
}

inline void NodeMultiFunctionBuilder::set_matching_fn(const MultiFunction &fn)
{
  this->set_matching_fn(&fn);
}

template<typename T, typename... Args>
inline void NodeMultiFunctionBuilder::construct_and_set_matching_fn(Args &&...args)
{
  const T &fn = resource_scope_.construct<T>(std::forward<Args>(args)...);
  this->set_matching_fn(&fn);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #NodeMultiFunctions Inline Methods
 * \{ */

inline const MultiFunction *NodeMultiFunctions::try_get(const DNode &node) const
{
  return map_.lookup_default(node->bnode(), nullptr);
}

/** \} */

}  // namespace blender::nodes
