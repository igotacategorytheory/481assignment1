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
 *
 * Copyright 2011, Blender Foundation.
 */

#include "COM_ValueNode.h"
#include "COM_SetValueOperation.h"

namespace blender::compositor {

ValueNode::ValueNode(bNode *editor_node) : Node(editor_node)
{
  /* pass */
}

void ValueNode::convert_to_operations(NodeConverter &converter,
                                      const CompositorContext & /*context*/) const
{
  SetValueOperation *operation = new SetValueOperation();
  NodeOutput *output = this->get_output_socket(0);
  operation->set_value(output->get_editor_value_float());
  converter.add_operation(operation);

  converter.map_output_socket(output, operation->get_output_socket());
}

}  // namespace blender::compositor
