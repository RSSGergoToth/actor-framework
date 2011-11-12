/******************************************************************************
 * This example shows how to implement serialize/deserialize to announce      *
 * non-trivial data structures to the libcppa type system.                    *
 *                                                                            *
 * However, announce() auto-detects STL compliant containers and provides     *
 * an easy way to tell libcppa how to serialize user defined types.           *
 * See announce_example 1-4 for usage examples.                               *
 *                                                                            *
 * You should use "hand written" serialize/deserialize implementations        *
 * if and only if there is no other way.                                      *
 ******************************************************************************/

#include <cstdint>
#include <iostream>
#include "cppa/cppa.hpp"
#include "cppa/to_string.hpp"

using std::cout;
using std::endl;

using namespace cppa;

// a node containing an integer and a vector of children
struct tree_node
{
    std::uint32_t value;
    std::vector<tree_node> children;

    tree_node(std::uint32_t v = 0) : value(v)
    {
    }

    tree_node& add_child(int v = 0)
    {
        children.push_back({ v });
        return *this;
    }

    tree_node(const tree_node&) = default;

    // recursively print this node and all of its children to stdout
    void print() const
    {
        // format is: value { children0, children1, ..., childrenN }
        // e.g., 10 { 20 { 21, 22 }, 30 }
        cout << value;
        if (children.empty() == false)
        {
            cout << " { ";
            auto begin = children.begin();
            auto end = children.end();
            for (auto i = begin; i != end; ++i)
            {
                if (i != begin)
                {
                    cout << ", ";
                }
                i->print();
            }
            cout << " } ";
        }
    }

};

// a very primitive tree implementation
struct tree
{
    tree_node root;

    // print tree to stdout
    void print() const
    {
        cout << "tree::print: ";
        root.print();
        cout << endl;
    }

};

// tree node are equals if values and all values of all children are equal
bool operator==(const tree_node& lhs, const tree_node& rhs)
{
    if (lhs.value == rhs.value && lhs.children.size() == rhs.children.size())
    {
        for (std::uint32_t i = 0; i < lhs.children.size(); ++i)
        {
            if (!(lhs.children[i] == rhs.children[i])) return false;
        }
        return true;
    }
    return false;
}

bool operator==(const tree& lhs, const tree& rhs)
{
    return lhs.root == rhs.root;
}

// abstract_uniform_type_info implements all functions of uniform_type_info
// except for serialize() and deserialize() if the template parameter T:
// - does have a default constructor
// - does have a copy constructor
// - does provide operator==
class tree_type_info : public util::abstract_uniform_type_info<tree>
{

 protected:

    void serialize(const void* ptr, serializer* sink) const
    {
        // ptr is guaranteed to be a pointer of type tree
        auto tree_ptr = reinterpret_cast<const tree*>(ptr);
        // serialization always begins with begin_object(name())
        // and ends with end_object();
        // name() returns the uniform type name of tree
        sink->begin_object(name());
        // recursively serialize nodes, beginning with root
        serialize_node(tree_ptr->root, sink);
        sink->end_object();
    }

    void deserialize(void* ptr, deserializer* source) const
    {
        // seek_object() gets the uniform name of the next object in the
        // stream without modifying the deserializer
        std::string cname = source->seek_object();
        // this name has to be our type name
        if (cname != name())
        {
            throw std::logic_error("wrong type name found");
        }
        // ptr is guaranteed to be a pointer of type tree
        auto tree_ptr = reinterpret_cast<tree*>(ptr);
        tree_ptr->root.children.clear();
        // workflow is analogous to serialize: begin_object() ... end_object()
        source->begin_object(cname);
        // recursively deserialize nodes, beginning with root
        deserialize_node(tree_ptr->root, source);
        source->end_object();
    }

 private:

    void serialize_node(const tree_node& node, serializer* sink) const
    {
        // serialize { value, number of children } ... children ...
        std::uint32_t num_children = node.children.size();
        sink->write_value(node.value);
        sink->write_value(num_children);
        for (const tree_node& subnode : node.children)
        {
            serialize_node(subnode, sink);
        }
    }

    void deserialize_node(tree_node& node, deserializer* source) const
    {
        // deserialize { value, number of children } ... children ...
        auto value = get<std::uint32_t>(source->read_value(pt_uint32));
        auto num_children = get<std::uint32_t>(source->read_value(pt_uint32));
        node.value = value;
        for (std::uint32_t i = 0; i < num_children; ++i)
        {
            node.add_child();
            deserialize_node(node.children.back(), source);
        }
    }

};

int main()
{
    // the tree_type_info is owned by libcppa after this function call
    announce(typeid(tree), new tree_type_info);

    tree t; // create a tree and fill it with some data

    t.root.add_child(10);
    t.root.children.back().add_child(11).add_child(12).add_child(13);

    t.root.add_child(20);
    t.root.children.back().add_child(21).add_child(22);

    /*
        tree t is now:
               0
              / \
             /   \
            /     \
          10       20
         / |\     /  \
        /  | \   /    \
       11 12 13 21    22
    */

    // send a tree to ourselves ...
    send(self(), t);

    receive
    (
        // ... and receive it
        on<tree>() >> [](const tree& tmsg)
        {
            // prints the tree in its serialized format:
            // @<> ( { tree ( 0, 2, 10, 3, 11, 0, 12, 0, 13, 0, 20, 2, 21, 0, 22, 0 ) } )
            cout << "to_string(last_received()): " << to_string(last_received())
                 << endl;
            // prints: 0 { 10 { 11, 12, 13 } , 20 { 21, 22 } }
            tmsg.print();
        }
    );

    return 0;
}
